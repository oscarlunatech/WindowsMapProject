#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "cartograph/map.h"

using namespace cartograph;
using Catch::Approx;

namespace {

std::string countriesPath() {
    return std::string(CARTOGRAPH_TEST_FIXTURES_DIR) + "/ne_110m_admin_0_countries.shp";
}

}  // namespace

TEST_CASE("A map reports the display CRS it was opened in", "[map]") {
    REQUIRE(Map::open(countriesPath(), "EPSG:4326").displayCrs() == "EPSG:4326");
    REQUIRE(Map::open(countriesPath(), "EPSG:3857").displayCrs() == "EPSG:3857");
    // The default is Web Mercator - what basemap tiles use.
    REQUIRE(Map::open(countriesPath()).displayCrs() == std::string(Dataset::defaultDisplayCrs()));
}

TEST_CASE("The display CRS determines the units of the map extent", "[map]") {
    const Envelope degrees = Map::open(countriesPath(), "EPSG:4326").extent();
    const Envelope metres = Map::open(countriesPath(), "EPSG:3857").extent();

    // Degrees: the fixture spans the whole world, poles included.
    REQUIRE(degrees.minX == Approx(-180.0).margin(0.001));
    REQUIRE(degrees.maxX == Approx(180.0).margin(0.001));
    REQUIRE(degrees.minY == Approx(-90.0).margin(0.001));

    // Metres: and crucially the southern edge is the *clamped* Web Mercator
    // bound, not PROJ's raw -242,529,000 extrapolation of latitude -90. This
    // is the assertion that fails if crs::Transformer's area-of-use clamp is
    // ever removed - the map would still load, it would just be unusable.
    REQUIRE(metres.minX == Approx(-20037508.0).margin(20000.0));
    REQUIRE(metres.maxX == Approx(20037508.0).margin(20000.0));
    REQUIRE(metres.minY == Approx(-20037508.0).margin(20000.0));
    REQUIRE(metres.minY > -21000000.0);
}

TEST_CASE("setDisplayCrs reprojects the whole map", "[map]") {
    Map map = Map::open(countriesPath(), "EPSG:4326");
    const std::size_t featureCount = map.featureCount();
    REQUIRE(map.extent().maxX == Approx(180.0).margin(0.001));

    map.setDisplayCrs("EPSG:3857");

    REQUIRE(map.displayCrs() == "EPSG:3857");
    REQUIRE(map.featureCount() == featureCount);  // same data, new coordinates
    REQUIRE(map.extent().maxX == Approx(20037508.0).margin(20000.0));

    // And back again: re-reading from source means round-tripping doesn't
    // accumulate the error a transform-the-transformed approach would.
    map.setDisplayCrs("EPSG:4326");
    REQUIRE(map.extent().maxX == Approx(180.0).margin(0.001));
    REQUIRE(map.extent().minY == Approx(-90.0).margin(0.001));
}

TEST_CASE("setDisplayCrs preserves per-layer visibility and opacity", "[map]") {
    Map map = Map::open(std::vector<std::string>{countriesPath(), countriesPath()}, "EPSG:4326");
    map.layers()[0].setVisible(false);
    map.layers()[1].setOpacity(0.25f);

    map.setDisplayCrs("EPSG:3857");

    REQUIRE(map.layers().size() == 2);
    REQUIRE_FALSE(map.layers()[0].visible());
    REQUIRE(map.layers()[1].opacity() == 0.25f);
}

TEST_CASE("setDisplayCrs to the current CRS is a no-op", "[map]") {
    Map map = Map::open(countriesPath(), "EPSG:4326");
    const Envelope before = map.extent();

    map.setDisplayCrs("EPSG:4326");

    REQUIRE(map.displayCrs() == "EPSG:4326");
    REQUIRE(map.extent().minX == before.minX);
    REQUIRE(map.extent().maxY == before.maxY);
}

TEST_CASE("A rejected display CRS leaves the map untouched", "[map]") {
    Map map = Map::open(countriesPath(), "EPSG:4326");
    const Envelope before = map.extent();

    REQUIRE_THROWS(map.setDisplayCrs("EPSG:NOT-A-REAL-CRS"));

    // Strong exception guarantee: the reload is built fully before anything
    // is swapped in, so a failure can't leave a half-reprojected map.
    REQUIRE(map.displayCrs() == "EPSG:4326");
    REQUIRE(map.layers().size() == 1);
    REQUIRE(map.extent().minX == before.minX);
    REQUIRE(map.extent().maxY == before.maxY);
}
