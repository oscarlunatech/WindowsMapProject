#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "cartograph/index/spatial_index.h"

using namespace cartograph;
using namespace cartograph::index;

namespace {

Feature makePointFeature(std::int64_t id, double x, double y) {
    Geometry geom(GeometryType::Point, {Part{Ring{Point2D{x, y}}}});
    return Feature(id, std::move(geom), {});
}

Envelope makeExtent(double minX, double minY, double maxX, double maxY) {
    Envelope e;
    e.expand(Point2D{minX, minY});
    e.expand(Point2D{maxX, maxY});
    return e;
}

}  // namespace

TEST_CASE("SpatialIndex query returns only features intersecting the query extent", "[index]") {
    std::vector<Feature> features;
    features.push_back(makePointFeature(0, 0, 0));
    features.push_back(makePointFeature(1, 10, 10));
    features.push_back(makePointFeature(2, 100, 100));

    Layer layer("points", {}, std::move(features), makeExtent(0, 0, 100, 100), "");

    const SpatialIndex index(layer);
    const std::vector<std::size_t> result = index.query(makeExtent(-1, -1, 11, 11));

    REQUIRE(result.size() == 2);
    REQUIRE(std::find(result.begin(), result.end(), 0) != result.end());
    REQUIRE(std::find(result.begin(), result.end(), 1) != result.end());
    REQUIRE(std::find(result.begin(), result.end(), 2) == result.end());
}

TEST_CASE("SpatialIndex query over the full extent returns every feature", "[index]") {
    std::vector<Feature> features;
    features.push_back(makePointFeature(0, 0, 0));
    features.push_back(makePointFeature(1, 50, 50));

    const Envelope layerExtent = makeExtent(0, 0, 50, 50);
    Layer layer("points", {}, std::move(features), layerExtent, "");

    const SpatialIndex index(layer);
    const std::vector<std::size_t> result = index.query(layerExtent);

    REQUIRE(result.size() == 2);
}

TEST_CASE("SpatialIndex query outside all features returns nothing", "[index]") {
    std::vector<Feature> features;
    features.push_back(makePointFeature(0, 0, 0));

    Layer layer("points", {}, std::move(features), makeExtent(0, 0, 0, 0), "");

    const SpatialIndex index(layer);
    const std::vector<std::size_t> result = index.query(makeExtent(1000, 1000, 2000, 2000));

    REQUIRE(result.empty());
}
