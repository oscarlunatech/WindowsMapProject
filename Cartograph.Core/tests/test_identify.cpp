#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

#include "cartograph/map.h"
#include "cartograph/query/identify.h"

using namespace cartograph;
using namespace cartograph::query;
using Catch::Approx;

namespace {

std::string fixturePath(const std::string& name) {
    return std::string(CARTOGRAPH_TEST_FIXTURES_DIR) + "/" + name;
}

std::string countriesPath() { return fixturePath("ne_110m_admin_0_countries.shp"); }

// Opening the fixture and building its cache is the expensive part, so the
// read-only cases share one map.
//
// EPSG:4326 explicitly, not the Phase 10 default of EPSG:3857: every probe
// point below is a real-world lon/lat, and map coordinates have to be in the
// same units for those to mean anything.
const Map& countries() {
    static const Map map = Map::open(countriesPath(), "EPSG:4326");
    return map;
}

std::string nameOf(const Map& map, const Hit& hit) {
    const Layer& layer = map.layers()[hit.layerIndex].layer();
    for (std::size_t i = 0; i < layer.fields().size(); ++i) {
        if (layer.fields()[i].name == "NAME") {
            const AttributeValue& value = layer.features()[hit.featureIndex].attributes()[i];
            if (const auto* text = std::get_if<std::string>(&value)) {
                return *text;
            }
        }
    }
    return {};
}

}  // namespace

TEST_CASE("identify finds the country containing a point", "[identify]") {
    // Middle of France, comfortably inland. Fixture is EPSG:4326, so map
    // coordinates are plain lon/lat.
    const std::vector<Hit> hits = identify(countries(), Point2D{2.3, 46.5}, 0.0);

    REQUIRE(hits.size() == 1);
    REQUIRE(nameOf(countries(), hits[0]) == "France");
    REQUIRE(hits[0].distance == Approx(0.0));  // containment, not proximity
}

TEST_CASE("identify returns nothing in the open ocean", "[identify]") {
    // Middle of the South Pacific - no land, and no coastline within a degree.
    REQUIRE(identify(countries(), Point2D{-140.0, -30.0}, 0.0).empty());
    REQUIRE(identify(countries(), Point2D{-140.0, -30.0}, 1.0).empty());
}

TEST_CASE("identify tolerance reaches nearby features without containing them", "[identify]") {
    // A point just off the west coast of France, in the Bay of Biscay: not
    // inside any country, but close to one.
    const Point2D offshore{-3.0, 46.5};

    REQUIRE(identify(countries(), offshore, 0.0).empty());

    const std::vector<Hit> nearby = identify(countries(), offshore, 2.0);
    REQUIRE_FALSE(nearby.empty());
    REQUIRE(nearby[0].distance > 0.0);  // proximity, not containment
    REQUIRE(nearby[0].distance <= 2.0);
}

TEST_CASE("identify orders hits nearest first within a layer", "[identify]") {
    // A generous tolerance around a point in the Balkans picks up several
    // countries; they must come back in ascending distance order.
    const std::vector<Hit> hits = identify(countries(), Point2D{20.0, 43.0}, 3.0);

    REQUIRE(hits.size() > 1);
    for (std::size_t i = 1; i < hits.size(); ++i) {
        REQUIRE(hits[i - 1].distance <= hits[i].distance);
    }
}

TEST_CASE("identify uses exact geometry, not the feature's bounding box", "[identify]") {
    // The R-tree can only narrow candidates by extent. Norway's bounding box
    // spans a huge area of sea because of Svalbard and the fjords, so a point
    // inside that box but not on land must not register as a hit - this is
    // the assertion that would fail if the exact geom::distanceTo test were
    // ever dropped in favour of trusting the index.
    const std::vector<Hit> hits = identify(countries(), Point2D{5.0, 66.0}, 0.0);

    for (const Hit& hit : hits) {
        REQUIRE(nameOf(countries(), hit) != "Norway");
    }
}

TEST_CASE("identify reports the topmost layer first", "[identify]") {
    // Same file stacked twice: layer 1 is drawn over layer 0, so a click that
    // hits both must report layer 1 first.
    const Map stacked = Map::open(std::vector<std::string>{countriesPath(), countriesPath()}, "EPSG:4326");
    REQUIRE(stacked.layers().size() == 2);

    const std::vector<Hit> hits = identify(stacked, Point2D{2.3, 46.5}, 0.0);

    REQUIRE(hits.size() == 2);
    REQUIRE(hits[0].layerIndex == 1);
    REQUIRE(hits[1].layerIndex == 0);
    REQUIRE(nameOf(stacked, hits[0]) == "France");
}

TEST_CASE("identify skips hidden layers", "[identify]") {
    Map stacked = Map::open(std::vector<std::string>{countriesPath(), countriesPath()}, "EPSG:4326");
    const Point2D inFrance{2.3, 46.5};

    REQUIRE(identify(stacked, inFrance, 0.0).size() == 2);

    // Hiding the top layer leaves only the bottom one answering.
    stacked.layers()[1].setVisible(false);
    const std::vector<Hit> oneVisible = identify(stacked, inFrance, 0.0);
    REQUIRE(oneVisible.size() == 1);
    REQUIRE(oneVisible[0].layerIndex == 0);

    // Hiding everything means clicking reports nothing at all - you can't
    // identify what you can't see.
    stacked.layers()[0].setVisible(false);
    REQUIRE(identify(stacked, inFrance, 0.0).empty());
}
