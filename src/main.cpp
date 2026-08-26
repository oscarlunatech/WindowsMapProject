#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "cartograph/crs/transformer.h"
#include "cartograph/dataset.h"
#include "cartograph/jobs/thread_pool.h"
#include "cartograph/map.h"
#include "cartograph/query/identify.h"
#include "cartograph/render/renderer.h"
#include "cartograph/style/stylesheet.h"
#include "viewer.h"

using namespace cartograph;

namespace {

std::string formatAttribute(const AttributeValue& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "(null)";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else {
                return std::format("{}", v);
            }
        },
        value);
}

// Every command takes one or more dataset paths before its flags, so the paths
// are everything from argv[2] up to the first argument starting with '-'.
std::vector<std::string> collectPaths(int argc, char** argv) {
    std::vector<std::string> paths;
    for (int i = 2; i < argc; ++i) {
        if (argv[i][0] == '-') {
            break;
        }
        paths.push_back(argv[i]);
    }
    return paths;
}

// Builds the stylesheet a draw command should use: the --style file if one was
// given, otherwise style::Symbol's built-in defaults.
style::Stylesheet buildStylesheet(const Map& map, const std::string& stylePath) {
    if (stylePath.empty()) {
        return style::Stylesheet::defaults(map);
    }
    return style::Stylesheet(style::loadStyleSpec(stylePath), map);
}

// info and dump are metadata commands: they read layer names, fields and
// attributes and never draw or hit-test anything. They deliberately go through
// Dataset rather than Map, because building a Map also builds every layer's
// R-tree and simplification buckets - a lot of work to print a field list.
int runInfo(const std::vector<std::string>& paths) {
    for (const std::string& path : paths) {
        const Dataset dataset = Dataset::open(path);
        if (paths.size() > 1) {
            std::cout << std::format("{}:\n", path);
        }
        for (const Layer& layer : dataset.layers()) {
            const Envelope& extent = layer.extent();
            std::cout << std::format(
                "layer: {}\n"
                "  features: {}\n"
                "  extent: [{}, {}] x [{}, {}]\n"
                "  crs: {}\n",
                layer.name(), layer.features().size(), extent.minX, extent.maxX, extent.minY,
                extent.maxY, layer.crsWkt().empty() ? "(none)" : layer.crsWkt());
        }
    }
    return 0;
}

int runDump(const std::vector<std::string>& paths, std::size_t limit) {
    for (const std::string& path : paths) {
        const Dataset dataset = Dataset::open(path);
        for (const Layer& layer : dataset.layers()) {
            std::cout << std::format("layer: {}\n", layer.name());
            const auto& fields = layer.fields();

            std::size_t count = 0;
            for (const Feature& feature : layer.features()) {
                if (count >= limit) {
                    break;
                }
                std::cout << std::format("  feature {}:\n", feature.id());
                const auto& attributes = feature.attributes();
                for (std::size_t i = 0; i < fields.size(); ++i) {
                    std::cout << std::format("    {}: {}\n", fields[i].name, formatAttribute(attributes[i]));
                }
                ++count;
            }
        }
    }
    return 0;
}

// Lists the map's layer stack in draw order, which is what a layer panel will
// eventually show - bottom layer first, topmost last.
int runLayers(const std::vector<std::string>& paths, const std::string& crs) {
    jobs::ThreadPool pool;
    const Map map = Map::open(paths, crs, pool);

    std::cout << std::format("{} layer(s), drawn bottom to top:\n", map.layers().size());
    for (std::size_t i = 0; i < map.layers().size(); ++i) {
        const MapLayer& mapLayer = map.layers()[i];
        std::cout << std::format("  [{}] {}  features: {}  source: {}\n", i, mapLayer.layer().name(),
                                  mapLayer.layer().features().size(), mapLayer.sourcePath());
    }
    return 0;
}

Point2D parsePoint(const std::string& text) {
    const auto comma = text.find(',');
    if (comma == std::string::npos) {
        throw std::invalid_argument("--at must be x,y");
    }
    return Point2D{std::stod(text.substr(0, comma)), std::stod(text.substr(comma + 1))};
}

// With no window there's no pixel size to derive a click tolerance from, so
// scale it to the data instead: 0.1% of the map extent's diagonal. Same trick
// LayerCache uses to pick its simplification tolerance buckets.
double defaultTolerance(const Envelope& extent) {
    if (!extent.valid) {
        return 0.0;
    }
    return 0.001 * std::hypot(extent.width(), extent.height());
}

int runIdentify(const std::vector<std::string>& paths, const std::string& crs, Point2D at,
                std::optional<double> toleranceOverride,
                std::size_t limit) {
    jobs::ThreadPool pool;
    const Map map = Map::open(paths, crs, pool);
    const double tolerance = toleranceOverride ? *toleranceOverride : defaultTolerance(map.extent());

    const std::vector<query::Hit> hits = query::identify(map, at, tolerance);

    std::cout << std::format("identify at ({}, {})  tolerance: {}  hits: {}\n", at.x, at.y, tolerance,
                              hits.size());

    std::size_t shown = 0;
    for (const query::Hit& hit : hits) {
        if (shown >= limit) {
            std::cout << std::format("  ... and {} more (raise --limit to see them)\n", hits.size() - shown);
            break;
        }
        const Layer& layer = map.layers()[hit.layerIndex].layer();
        const Feature& feature = layer.features()[hit.featureIndex];
        std::cout << std::format("  [{}] {} feature {}  distance: {}\n", hit.layerIndex, layer.name(),
                                  feature.id(), hit.distance);
        for (std::size_t i = 0; i < layer.fields().size() && i < feature.attributes().size(); ++i) {
            std::cout << std::format("    {}: {}\n", layer.fields()[i].name,
                                      formatAttribute(feature.attributes()[i]));
        }
        ++shown;
    }
    return 0;
}

Envelope parseBBox(const std::string& text) {
    std::vector<double> values;
    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, ',')) {
        values.push_back(std::stod(token));
    }
    if (values.size() != 4) {
        throw std::invalid_argument("--bbox must be minX,minY,maxX,maxY");
    }
    Envelope bbox;
    bbox.expand(Point2D{values[0], values[1]});
    bbox.expand(Point2D{values[2], values[3]});
    return bbox;
}

render::ScreenSize parseSize(const std::string& text) {
    const auto xPos = text.find('x');
    if (xPos == std::string::npos) {
        throw std::invalid_argument("--size must be WIDTHxHEIGHT");
    }
    return render::ScreenSize{std::stoi(text.substr(0, xPos)), std::stoi(text.substr(xPos + 1))};
}

int runRender(const std::vector<std::string>& paths, const std::string& crs,
              const std::optional<Envelope>& bboxOverride,
              render::ScreenSize size, const std::string& outputPath, const std::string& stylePath) {
    jobs::ThreadPool pool;
    const Map map = Map::open(paths, crs, pool);
    const Envelope bbox = bboxOverride ? *bboxOverride : map.extent();
    const render::Viewport viewport(bbox, size);
    const auto bitmap = render::Renderer::render(map, viewport, buildStylesheet(map, stylePath));
    render::savePng(bitmap.Get(), outputPath);
    return 0;
}

// Interpolates from the map's full extent down to a zoomed-in view of
// Newark/Jersey City (the densest part of the NJ roads benchmark dataset),
// giving a fixed, reproducible camera path: frame 0 is the worst case for an
// unindexed draw (everything visible), the last frame is where culling and
// simplification should matter least (small, already-dense view).
//
// The target is written as lon/lat because that's how anyone reading it can
// tell where it is, then transformed into whatever CRS the map is actually in
// - otherwise passing --crs would silently aim the camera at a point in the
// Atlantic (or, in EPSG:3857 metres, a point 74 metres from the origin).
std::vector<Envelope> buildCameraPath(const Envelope& fullExtent, int frameCount, const std::string& crs) {
    const Point2D newarkLonLat{-74.19, 40.74};
    const crs::Transformer toMapCrs("EPSG:4326", crs);

    // A tenth of a degree around Newark, expressed in the map's own units by
    // transforming the corners rather than assuming degrees.
    const Point2D lowerLeft = toMapCrs.transform(Point2D{newarkLonLat.x - 0.05, newarkLonLat.y - 0.05});
    const Point2D upperRight = toMapCrs.transform(Point2D{newarkLonLat.x + 0.05, newarkLonLat.y + 0.05});

    Envelope zoomed;
    zoomed.expand(lowerLeft);
    zoomed.expand(upperRight);

    std::vector<Envelope> path;
    path.reserve(static_cast<std::size_t>(frameCount));
    for (int i = 0; i < frameCount; ++i) {
        const double t = frameCount > 1 ? static_cast<double>(i) / static_cast<double>(frameCount - 1) : 0.0;
        Envelope frame;
        frame.expand(Point2D{fullExtent.minX + (zoomed.minX - fullExtent.minX) * t,
                              fullExtent.minY + (zoomed.minY - fullExtent.minY) * t});
        frame.expand(Point2D{fullExtent.maxX + (zoomed.maxX - fullExtent.maxX) * t,
                              fullExtent.maxY + (zoomed.maxY - fullExtent.maxY) * t});
        path.push_back(frame);
    }
    return path;
}

int runBench(const std::vector<std::string>& paths, const std::string& crs, int frameCount, bool culled,
             const std::string& csvPath,
             const std::string& stylePath) {
    // Map::open builds every layer's R-tree and simplification buckets, in
    // parallel across the pool, before the timed loop starts - the benchmark
    // measures per-frame draw cost, not index construction. Same for the
    // stylesheet, which resolves every feature's symbol up front so the
    // per-frame path only does an array lookup.
    jobs::ThreadPool pool;
    const Map map = Map::open(paths, crs, pool);
    const style::Stylesheet stylesheet = buildStylesheet(map, stylePath);
    const std::vector<Envelope> cameraPath = buildCameraPath(map.extent(), frameCount, crs);

    const render::ScreenSize size{1024, 768};
    render::OffscreenTarget target(size);

    std::ofstream csv(csvPath);
    if (!csv) {
        throw std::runtime_error("failed to open '" + csvPath + "' for writing");
    }
    csv << "frame,ms\n";

    std::vector<double> timings;
    timings.reserve(static_cast<std::size_t>(frameCount));

    for (int i = 0; i < frameCount; ++i) {
        const render::Viewport viewport(cameraPath[static_cast<std::size_t>(i)], size);

        const auto start = std::chrono::high_resolution_clock::now();
        target.beginFrame();
        if (culled) {
            render::drawMapCulled(target.renderTarget(), target.factory(), map, viewport, pool, stylesheet);
        } else {
            render::drawMap(target.renderTarget(), target.factory(), map, viewport, stylesheet);
        }
        target.endFrame();
        const auto end = std::chrono::high_resolution_clock::now();

        const double ms = std::chrono::duration<double, std::milli>(end - start).count();
        timings.push_back(ms);
        csv << i << "," << ms << "\n";
    }

    const double avg = std::accumulate(timings.begin(), timings.end(), 0.0) /
                        static_cast<double>(timings.size());
    const auto [minIt, maxIt] = std::minmax_element(timings.begin(), timings.end());
    std::cout << std::format("frames: {}  culled: {}  avg: {:.2f}ms  min: {:.2f}ms  max: {:.2f}ms  -> {}\n",
                              frameCount, culled, avg, *minIt, *maxIt, csvPath);
    return 0;
}

int runView(const std::vector<std::string>& paths, const std::string& crs, const std::string& stylePath) {
    Viewer viewer(paths, crs, stylePath);
    viewer.run();
    return 0;
}

void printUsage(const char* argv0) {
    std::cerr << std::format(
        "usage: {0} info <path>... \n"
        "       {0} dump <path>... [--limit N]\n"
        "       {0} layers <path>... [--crs EPSG:NNNN]\n"
        "       {0} identify <path>... --at x,y [--crs EPSG:NNNN] [--tolerance N] [--limit N]\n"
        "       {0} render <path>... [--bbox minX,minY,maxX,maxY] [--size WxH] [--crs EPSG:NNNN] "
        "[--style s.json] -o <output.png>\n"
        "       {0} view <path>... [--crs EPSG:NNNN] [--style s.json]\n"
        "       {0} bench <path>... [--frames N] [--culled] [--crs EPSG:NNNN] [--style s.json] "
        "[-o results.csv]\n"
        "\n"
        "Multiple paths stack as layers, first path at the bottom.\n"
        "--crs sets the display CRS every layer is reprojected into; --bbox and --at are read in\n"
        "that CRS. Defaults to {1} ({2} for bench, to keep its numbers comparable).\n",
        argv0, Dataset::defaultDisplayCrs(), "EPSG:4326");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string command = argv[1];
    const std::vector<std::string> paths = collectPaths(argc, argv);
    if (paths.empty()) {
        std::cerr << "error: at least one dataset path is required\n";
        return 1;
    }
    const int flagStart = 2 + static_cast<int>(paths.size());

    // --crs applies to every command that builds a Map, so it's parsed once
    // here rather than repeated in each branch.
    std::optional<std::string> crsOverride;
    for (int i = flagStart; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--crs" && i + 1 < argc) {
            crsOverride = argv[i + 1];
        }
    }
    // bench defaults to EPSG:4326 rather than the usual EPSG:3857, so its
    // numbers stay comparable with every figure already recorded in
    // BENCHMARKS.md - reprojecting the dataset would change what's being
    // measured. Pass --crs explicitly to benchmark a different projection.
    const std::string crs = crsOverride ? *crsOverride
                            : command == "bench" ? std::string("EPSG:4326")
                                                  : std::string(Dataset::defaultDisplayCrs());

    try {
        if (command == "info") {
            return runInfo(paths);
        }
        if (command == "layers") {
            return runLayers(paths, crs);
        }
        if (command == "dump") {
            std::size_t limit = 10;
            for (int i = flagStart; i < argc; ++i) {
                const std::string_view arg = argv[i];
                if (arg == "--limit" && i + 1 < argc) {
                    limit = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
                }
            }
            return runDump(paths, limit);
        }
        if (command == "identify") {
            std::optional<Point2D> at;
            std::optional<double> tolerance;
            std::size_t limit = 3;
            for (int i = flagStart; i < argc; ++i) {
                const std::string_view arg = argv[i];
                if (arg == "--at" && i + 1 < argc) {
                    at = parsePoint(argv[++i]);
                } else if (arg == "--tolerance" && i + 1 < argc) {
                    tolerance = std::stod(argv[++i]);
                } else if (arg == "--limit" && i + 1 < argc) {
                    limit = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
                }
            }
            if (!at) {
                std::cerr << "error: --at x,y is required\n";
                return 1;
            }
            return runIdentify(paths, crs, *at, tolerance, limit);
        }
        if (command == "render") {
            std::optional<Envelope> bbox;
            render::ScreenSize size{1024, 768};
            std::string output;
            std::string style;
            for (int i = flagStart; i < argc; ++i) {
                const std::string_view arg = argv[i];
                if (arg == "--bbox" && i + 1 < argc) {
                    bbox = parseBBox(argv[++i]);
                } else if (arg == "--size" && i + 1 < argc) {
                    size = parseSize(argv[++i]);
                } else if (arg == "--style" && i + 1 < argc) {
                    style = argv[++i];
                } else if (arg == "-o" && i + 1 < argc) {
                    output = argv[++i];
                }
            }
            if (output.empty()) {
                std::cerr << "error: -o <output.png> is required\n";
                return 1;
            }
            return runRender(paths, crs, bbox, size, output, style);
        }
        if (command == "view") {
            std::string style;
            for (int i = flagStart; i < argc; ++i) {
                const std::string_view arg = argv[i];
                if (arg == "--style" && i + 1 < argc) {
                    style = argv[++i];
                }
            }
            return runView(paths, crs, style);
        }
        if (command == "bench") {
            int frames = 60;
            bool culled = false;
            std::string output = "bench_results.csv";
            std::string style;
            for (int i = flagStart; i < argc; ++i) {
                const std::string_view arg = argv[i];
                if (arg == "--frames" && i + 1 < argc) {
                    frames = std::atoi(argv[++i]);
                } else if (arg == "--culled") {
                    culled = true;
                } else if (arg == "--style" && i + 1 < argc) {
                    style = argv[++i];
                } else if (arg == "-o" && i + 1 < argc) {
                    output = argv[++i];
                }
            }
            return runBench(paths, crs, frames, culled, output, style);
        }
    } catch (const std::exception& e) {
        std::cerr << std::format("error: {}\n", e.what());
        return 1;
    }

    printUsage(argv[0]);
    return 1;
}
