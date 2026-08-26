#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cpl_conv.h>
#include <gdal_priv.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "cartograph/raster/raster_source.h"

using namespace cartograph;
using namespace cartograph::raster;
using Catch::Approx;

namespace {

// Writes a small GeoTIFF and deletes it again when the test ends.
//
// Generated rather than committed: the fixture tier table says only small,
// stable data belongs in the repo, and a synthetic raster keeps the suite
// self-contained without adding a binary blob. The real Natural Earth raster
// (scripts/fetch-raster.ps1) is for looking at, not for asserting on.
class TemporaryRaster {
public:
    // A global raster, one band, `size` x `size`, spanning the whole world so
    // reprojection tests actually reach the poles. Pixel value = row * size +
    // column, so every pixel is individually identifiable.
    explicit TemporaryRaster(int size = 8) : size_(size) {
        path_ = (std::filesystem::temp_directory_path() /
                 ("cartograph_test_" + std::to_string(counter()++) + ".tif"))
                    .string();

        // This helper drives GDAL directly to *write* a file, which Core never
        // does, so it can't go through the library's own setup - and without
        // PROJ_DATA pointing at the proj.db beside the binary, SetProjection
        // below prints "Cannot find proj.db" to stderr. Harmless, but stderr
        // noise during a green test run is how real errors get missed.
        static const bool projConfigured = [] {
            char exePath[1024];
            if (CPLGetExecPath(exePath, sizeof(exePath))) {
                CPLSetConfigOption("PROJ_DATA", CPLGetDirname(exePath));
            }
            return true;
        }();
        (void)projConfigured;

        GDALAllRegister();
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        REQUIRE(driver != nullptr);

        GDALDataset* dataset = driver->Create(path_.c_str(), size_, size_, 1, GDT_Float64, nullptr);
        REQUIRE(dataset != nullptr);

        // North-up, whole world: 360 degrees across, 180 down.
        double transform[6] = {-180.0, 360.0 / size_, 0.0, 90.0, 0.0, -180.0 / size_};
        dataset->SetGeoTransform(transform);
        dataset->SetProjection("EPSG:4326");

        std::vector<double> values(static_cast<std::size_t>(size_) * size_);
        for (int row = 0; row < size_; ++row) {
            for (int col = 0; col < size_; ++col) {
                values[static_cast<std::size_t>(row) * size_ + col] = row * size_ + col;
            }
        }
        REQUIRE(dataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, size_, size_, values.data(), size_,
                                                     size_, GDT_Float64, 0, 0) == CE_None);
        GDALClose(dataset);
    }

    ~TemporaryRaster() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    TemporaryRaster(const TemporaryRaster&) = delete;
    TemporaryRaster& operator=(const TemporaryRaster&) = delete;

    const std::string& path() const { return path_; }
    int size() const { return size_; }

private:
    static int& counter() {
        static int value = 0;
        return value;
    }

    std::string path_;
    int size_;
};

}  // namespace

TEST_CASE("A raster reports its size, bands and georeferenced extent", "[raster]") {
    const TemporaryRaster file;
    const RasterSource source(file.path(), "EPSG:4326");

    REQUIRE(source.width() == file.size());
    REQUIRE(source.height() == file.size());
    REQUIRE(source.bandCount() == 1);
    REQUIRE(source.hasCrs());

    // Same CRS in and out, so no warp - the grid is untouched.
    REQUIRE(source.extent().minX == Approx(-180.0));
    REQUIRE(source.extent().maxX == Approx(180.0));
    REQUIRE(source.extent().minY == Approx(-90.0));
    REQUIRE(source.extent().maxY == Approx(90.0));
}

TEST_CASE("sample returns the raw band value under a point", "[raster]") {
    const TemporaryRaster file(4);
    const RasterSource source(file.path(), "EPSG:4326");

    // 4x4 over the world: each pixel is 90 degrees wide, 45 tall. Value is
    // row * 4 + column, with row 0 at the top (north).
    REQUIRE(source.sample(Point2D{-170.0, 89.0}).at(0) == Approx(0.0));    // row 0, col 0
    REQUIRE(source.sample(Point2D{170.0, 89.0}).at(0) == Approx(3.0));     // row 0, col 3
    REQUIRE(source.sample(Point2D{-170.0, -89.0}).at(0) == Approx(12.0));  // row 3, col 0
    REQUIRE(source.sample(Point2D{170.0, -89.0}).at(0) == Approx(15.0));   // row 3, col 3

    // Raw values, not stretched to display range - identify should report what
    // the file says.
    REQUIRE(source.sample(Point2D{10.0, 10.0}).size() == 1);

    // Outside the raster entirely.
    REQUIRE(source.sample(Point2D{500.0, 0.0}).empty());
}

TEST_CASE("read returns premultiplied BGRA sized to the request", "[raster]") {
    const TemporaryRaster file(64);
    const RasterSource source(file.path(), "EPSG:4326");

    const RasterImage image = source.read(source.extent(), 32, 32, RasterStyle{});

    REQUIRE(image.width == 32);
    REQUIRE(image.height == 32);
    REQUIRE(image.bgra.size() == static_cast<std::size_t>(32) * 32 * 4);
    REQUIRE(image.version > 0);
    REQUIRE(image.extent.minX == Approx(-180.0));
    REQUIRE(image.extent.maxY == Approx(90.0));

    // One band renders as grayscale, so red, green and blue agree everywhere.
    for (std::size_t p = 0; p < image.bgra.size(); p += 4) {
        REQUIRE(image.bgra[p] == image.bgra[p + 1]);
        REQUIRE(image.bgra[p + 1] == image.bgra[p + 2]);
        REQUIRE(image.bgra[p + 3] == 255);
    }
}

TEST_CASE("read never returns more pixels than the raster has", "[raster]") {
    const TemporaryRaster file(8);
    const RasterSource source(file.path(), "EPSG:4326");

    // Asking for far more than the source holds must not upsample - that's the
    // renderer's job, and inventing pixels here would waste both memory and
    // the read.
    const RasterImage image = source.read(source.extent(), 4096, 4096, RasterStyle{});
    REQUIRE(image.width == 8);
    REQUIRE(image.height == 8);
}

TEST_CASE("read reports the overlap it actually covered, not what was asked for", "[raster]") {
    const TemporaryRaster file(8);
    const RasterSource source(file.path(), "EPSG:4326");

    // A window hanging off the eastern edge. The image has to describe the
    // ground it really covers, or the renderer would place it wrongly.
    Envelope window;
    window.expand(Point2D{90.0, -45.0});
    window.expand(Point2D{400.0, 45.0});

    const RasterImage image = source.read(window, 256, 256, RasterStyle{});
    REQUIRE_FALSE(image.empty());
    REQUIRE(image.extent.maxX <= Approx(180.0));
    REQUIRE(image.extent.minX >= Approx(45.0));  // snapped outward to whole pixels

    // A window that misses entirely comes back empty rather than blank.
    Envelope elsewhere;
    elsewhere.expand(Point2D{1000.0, 1000.0});
    elsewhere.expand(Point2D{2000.0, 2000.0});
    REQUIRE(source.read(elsewhere, 256, 256, RasterStyle{}).empty());
}

TEST_CASE("stretch modes change the mapping from value to pixel", "[raster]") {
    const TemporaryRaster file(8);  // values 0..63
    const RasterSource source(file.path(), "EPSG:4326");

    RasterStyle none;
    none.stretch = StretchMode::None;
    const RasterImage raw = source.read(source.extent(), 8, 8, none);

    RasterStyle minMax;
    minMax.stretch = StretchMode::MinMax;
    const RasterImage stretched = source.read(source.extent(), 8, 8, minMax);

    // Unstretched, values 0..63 stay dark. MinMax spreads them across the full
    // range, so the brightest pixel saturates.
    REQUIRE(raw.bgra[0] == 0);
    REQUIRE(stretched.bgra[0] == 0);

    const std::size_t last = raw.bgra.size() - 4;
    REQUIRE(raw.bgra[last] == 63);
    REQUIRE(stretched.bgra[last] == 255);
}

TEST_CASE("Warping to Web Mercator clamps instead of running to 242 million", "[raster]") {
    // The raster form of the Phase 10 finding. GDALAutoCreateWarpedVRT would
    // derive its output grid by transforming this global raster's own corners,
    // reaching latitude +/-90 and so a Web Mercator extent twelve times too
    // tall - which would also cost most of the resolution, since the grid gets
    // sized to cover it. RasterSource computes the grid through
    // crs::Transformer instead, which clamps.
    const TemporaryRaster file(64);
    const RasterSource source(file.path(), "EPSG:3857");

    const Envelope& extent = source.extent();
    REQUIRE(extent.valid);
    REQUIRE(extent.minY > -21000000.0);
    REQUIRE(extent.maxY < 21000000.0);
    REQUIRE(extent.minX == Approx(-20037508.0).margin(20000.0));
    REQUIRE(extent.maxX == Approx(20037508.0).margin(20000.0));

    // Web Mercator's world is square, so a global source warps to a roughly
    // square grid - not the 1119x12023 sliver the unclamped version produces.
    const double aspect = static_cast<double>(source.width()) / source.height();
    REQUIRE(aspect == Approx(1.0).margin(0.05));

    // And the warp keeps roughly the source's detail rather than throwing it away.
    REQUIRE(source.width() > 32);
}

TEST_CASE("Opening a nonexistent or non-raster file throws", "[raster]") {
    REQUIRE_THROWS_AS(RasterSource("no_such_raster_file.tif", "EPSG:4326"), RasterError);
}

