#include <catch2/catch_test_macros.hpp>

#include "cartograph/dataset.h"

using namespace cartograph;

namespace {
std::string fixturePath(const std::string& name) {
    return std::string(CARTOGRAPH_TEST_FIXTURES_DIR) + "/" + name;
}
}  // namespace

TEST_CASE("Opening a nonexistent dataset throws", "[dataset]") {
    REQUIRE_THROWS_AS(Dataset::open(fixturePath("does_not_exist.shp")), DatasetOpenError);
}

TEST_CASE("Natural Earth countries fixture loads correctly", "[dataset]") {
    // EPSG:4326 explicitly: since Phase 10 the default is EPSG:3857, and what
    // this case exercises is the *identity*-transform path below.
    const Dataset dataset = Dataset::open(fixturePath("ne_110m_admin_0_countries.shp"), "EPSG:4326");

    REQUIRE(dataset.layers().size() == 1);
    const Layer& layer = dataset.layers().front();

    REQUIRE(layer.features().size() > 150);  // ~177 countries at 110m scale
    REQUIRE(layer.extent().valid);
    // The fixture's own .prj is already WGS84, so this exercises the
    // identity-transform path in dataset.cpp's reprojection (Phase 6) - the
    // label still reports EPSG:4326, confirming a transform was applied
    // (not just "left as whatever the source file said").
    REQUIRE(layer.crsWkt() == "EPSG:4326");
    REQUIRE_FALSE(layer.fields().empty());

    // The same fixture opened into a different display CRS reports that CRS
    // instead, and its coordinates really are in metres - crsWkt() tracks
    // where the geometry actually is, not what the .prj sidecar said.
    const Dataset webMercator =
        Dataset::open(fixturePath("ne_110m_admin_0_countries.shp"), "EPSG:3857");
    REQUIRE(webMercator.layers().front().crsWkt() == "EPSG:3857");
    REQUIRE(webMercator.extent().maxX > 1000000.0);

    for (const Feature& feature : layer.features()) {
        REQUIRE(feature.geometry().type() != GeometryType::Unknown);
    }
}
