#include <catch2/catch_test_macros.hpp>

#include "cartograph/geom/simplify.h"

using namespace cartograph;

TEST_CASE("simplify keeps a near-straight-line point within tolerance", "[simplify]") {
    // Perpendicular distance of the middle point from the (0,0)-(10,0) line is 0.1.
    const Ring ring = {Point2D{0, 0}, Point2D{5, 0.1}, Point2D{10, 0}};

    REQUIRE(geom::simplify(ring, 0.01).size() == 3);

    const Ring dropped = geom::simplify(ring, 1.0);
    REQUIRE(dropped.size() == 2);
    REQUIRE(dropped.front().x == 0);
    REQUIRE(dropped.back().x == 10);
}

TEST_CASE("simplify with zero tolerance returns the ring unchanged", "[simplify]") {
    const Ring ring = {Point2D{0, 0}, Point2D{1, 1}, Point2D{2, 0}};
    REQUIRE(geom::simplify(ring, 0.0).size() == ring.size());
}

TEST_CASE("simplify keeps a large deviation even at generous tolerance", "[simplify]") {
    // Perpendicular distance of the middle point from the (0,0)-(10,0) line is 100.
    const Ring ring = {Point2D{0, 0}, Point2D{5, 100}, Point2D{10, 0}};
    REQUIRE(geom::simplify(ring, 1.0).size() == 3);
}

TEST_CASE("Geometry simplify applies to every ring of every part", "[simplify]") {
    const Geometry multiLine(GeometryType::MultiLineString,
                              {
                                  Part{Ring{Point2D{0, 0}, Point2D{5, 0.1}, Point2D{10, 0}}},
                                  Part{Ring{Point2D{0, 0}, Point2D{5, 0.1}, Point2D{10, 0}}},
                              });

    const Geometry simplified = geom::simplify(multiLine, 1.0);
    REQUIRE(simplified.parts().size() == 2);
    REQUIRE(simplified.parts()[0][0].size() == 2);
    REQUIRE(simplified.parts()[1][0].size() == 2);
}
