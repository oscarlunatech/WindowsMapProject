#include <algorithm>
#include <chrono>
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

#include "cartograph/dataset.h"
#include "cartograph/jobs/thread_pool.h"
#include "cartograph/render/renderer.h"
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

int runInfo(const std::string& path) {
    const Dataset dataset = Dataset::open(path);
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
    return 0;
}

int runDump(const std::string& path, std::size_t limit) {
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

int runRender(const std::string& path, const std::optional<Envelope>& bboxOverride,
              render::ScreenSize size, const std::string& outputPath) {
    const Dataset dataset = Dataset::open(path);
    const Envelope bbox = bboxOverride ? *bboxOverride : dataset.extent();
    const render::Viewport viewport(bbox, size);
    const auto bitmap = render::Renderer::render(dataset, viewport);
    render::savePng(bitmap.Get(), outputPath);
    return 0;
}

// Interpolates from the dataset's full extent down to a zoomed-in view of
// Newark/Jersey City (the densest part of the NJ roads benchmark dataset),
// giving a fixed, reproducible camera path: frame 0 is the worst case for an
// unindexed draw (everything visible), the last frame is where culling and
// simplification should matter least (small, already-dense view).
std::vector<Envelope> buildCameraPath(const Envelope& fullExtent, int frameCount) {
    Envelope zoomed;
    zoomed.expand(Point2D{-74.19 - 0.05, 40.74 - 0.05});
    zoomed.expand(Point2D{-74.19 + 0.05, 40.74 + 0.05});

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

int runBench(const std::string& path, int frameCount, bool culled, const std::string& csvPath) {
    const Dataset dataset = Dataset::open(path);
    const std::vector<Envelope> cameraPath = buildCameraPath(dataset.extent(), frameCount);

    // Building the LayerCache (R-tree + precomputed simplification buckets)
    // happens once, outside the timed loop, same as a live Viewer would do
    // it at startup - the benchmark measures per-frame draw cost, not index
    // construction. Built in parallel across layers via the same
    // jobs::ThreadPool the timed loop below uses per-frame.
    jobs::ThreadPool pool;
    std::vector<render::LayerCache> layerCaches;
    if (culled) {
        layerCaches = render::buildLayerCachesParallel(pool, dataset.layers());
    }

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
            render::drawDatasetCulled(target.renderTarget(), target.factory(), dataset, layerCaches, viewport,
                                       pool);
        } else {
            render::drawDataset(target.renderTarget(), target.factory(), dataset, viewport);
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

int runView(const std::string& path) {
    Viewer viewer(path);
    viewer.run();
    return 0;
}

void printUsage(const char* argv0) {
    std::cerr << std::format(
        "usage: {} info <path>\n"
        "       {} dump <path> [--limit N]\n"
        "       {} render <path> [--bbox minX,minY,maxX,maxY] [--size WxH] -o <output.png>\n"
        "       {} view <path>\n"
        "       {} bench <path> [--frames N] [--culled] [-o results.csv]\n",
        argv0, argv0, argv0, argv0, argv0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string command = argv[1];
    const std::string path = argv[2];

    try {
        if (command == "info") {
            return runInfo(path);
        }
        if (command == "dump") {
            std::size_t limit = 10;
            for (int i = 3; i < argc; ++i) {
                const std::string_view arg = argv[i];
                if (arg == "--limit" && i + 1 < argc) {
                    limit = static_cast<std::size_t>(std::strtoul(argv[++i], nullptr, 10));
                }
            }
            return runDump(path, limit);
        }
        if (command == "render") {
            std::optional<Envelope> bbox;
            render::ScreenSize size{1024, 768};
            std::string output;
            for (int i = 3; i < argc; ++i) {
                const std::string_view arg = argv[i];
                if (arg == "--bbox" && i + 1 < argc) {
                    bbox = parseBBox(argv[++i]);
                } else if (arg == "--size" && i + 1 < argc) {
                    size = parseSize(argv[++i]);
                } else if (arg == "-o" && i + 1 < argc) {
                    output = argv[++i];
                }
            }
            if (output.empty()) {
                std::cerr << "error: -o <output.png> is required\n";
                return 1;
            }
            return runRender(path, bbox, size, output);
        }
        if (command == "view") {
            return runView(path);
        }
        if (command == "bench") {
            int frames = 60;
            bool culled = false;
            std::string output = "bench_results.csv";
            for (int i = 3; i < argc; ++i) {
                const std::string_view arg = argv[i];
                if (arg == "--frames" && i + 1 < argc) {
                    frames = std::atoi(argv[++i]);
                } else if (arg == "--culled") {
                    culled = true;
                } else if (arg == "-o" && i + 1 < argc) {
                    output = argv[++i];
                }
            }
            return runBench(path, frames, culled, output);
        }
    } catch (const std::exception& e) {
        std::cerr << std::format("error: {}\n", e.what());
        return 1;
    }

    printUsage(argv[0]);
    return 1;
}
