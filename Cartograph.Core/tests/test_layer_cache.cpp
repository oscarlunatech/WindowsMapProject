#include <catch2/catch_test_macros.hpp>

#include "cartograph/render/layer_cache.h"

using namespace cartograph;
using namespace cartograph::render;

namespace {

Feature makeLineFeature(std::int64_t id, Ring ring) {
    Geometry geom(GeometryType::LineString, {Part{std::move(ring)}});
    return Feature(id, std::move(geom), {});
}

Envelope makeExtent(double minX, double minY, double maxX, double maxY) {
    Envelope e;
    e.expand(Point2D{minX, minY});
    e.expand(Point2D{maxX, maxY});
    return e;
}

}  // namespace

TEST_CASE("LayerCache query culls to the given extent", "[layer_cache]") {
    std::vector<Feature> features;
    features.push_back(makeLineFeature(0, Ring{Point2D{0, 0}, Point2D{1, 1}}));
    features.push_back(makeLineFeature(1, Ring{Point2D{100, 100}, Point2D{101, 101}}));

    Layer layer("lines", {}, std::move(features), makeExtent(0, 0, 101, 101), "");
    const LayerCache cache(layer);

    const std::vector<std::size_t> visible = cache.query(makeExtent(-1, -1, 2, 2));

    REQUIRE(visible.size() == 1);
    REQUIRE(visible[0] == 0);
}

TEST_CASE("LayerCache returns coarser geometry at a coarser mapUnitsPerPixel", "[layer_cache]") {
    // Middle point deviates by exactly 1.0 from the (0,0)-(1000,0) line, which
    // is also the layer's coarsest precomputed tolerance (0.1% of a 1000-unit
    // diagonal) - so it survives at full detail and drops at the coarsest
    // bucket.
    std::vector<Feature> features;
    features.push_back(makeLineFeature(0, Ring{Point2D{0, 0}, Point2D{500, 1}, Point2D{1000, 0}}));

    Layer layer("lines", {}, std::move(features), makeExtent(0, 0, 1000, 0), "");
    const LayerCache cache(layer);

    REQUIRE(cache.simplifiedGeometry(0, 0.0).parts()[0][0].size() == 3);
    REQUIRE(cache.simplifiedGeometry(0, 10.0).parts()[0][0].size() == 2);
}
