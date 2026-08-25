#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "cartograph/geom/predicates.h"

using namespace cartograph;
using namespace cartograph::geom;
using Catch::Approx;

namespace {

// A 10x10 square with a 4x4 hole in the middle (from (3,3) to (7,7)).
Geometry squareWithHole() {
    Ring exterior{Point2D{0, 0}, Point2D{10, 0}, Point2D{10, 10}, Point2D{0, 10}};
    Ring hole{Point2D{3, 3}, Point2D{7, 3}, Point2D{7, 7}, Point2D{3, 7}};
    return Geometry(GeometryType::Polygon, {Part{exterior, hole}});
}

}  // namespace

TEST_CASE("distanceToSegment projects onto the segment and clamps to its ends", "[predicates]") {
    const Point2D a{0, 0};
    const Point2D b{10, 0};

    REQUIRE(distanceToSegment(Point2D{5, 3}, a, b) == Approx(3.0));   // perpendicular, mid-segment
    REQUIRE(distanceToSegment(Point2D{5, 0}, a, b) == Approx(0.0));   // on the segment
    REQUIRE(distanceToSegment(Point2D{-4, 0}, a, b) == Approx(4.0));  // past a, clamps to a
    REQUIRE(distanceToSegment(Point2D{14, 0}, a, b) == Approx(4.0));  // past b, clamps to b
    // Past the end diagonally: distance to the endpoint, not to the infinite line.
    REQUIRE(distanceToSegment(Point2D{13, 4}, a, b) == Approx(5.0));
    // Degenerate segment reduces to a point distance.
    REQUIRE(distanceToSegment(Point2D{3, 4}, a, a) == Approx(5.0));
}

TEST_CASE("pointInRing treats the ring as implicitly closed", "[predicates]") {
    // Same square, once without a repeated closing vertex and once with -
    // OGR usually writes the closing vertex, hand-built test data usually
    // doesn't, and both must behave identically.
    const Ring open{Point2D{0, 0}, Point2D{10, 0}, Point2D{10, 10}, Point2D{0, 10}};
    const Ring closed{Point2D{0, 0}, Point2D{10, 0}, Point2D{10, 10}, Point2D{0, 10}, Point2D{0, 0}};

    REQUIRE(pointInRing(Point2D{5, 5}, open));
    REQUIRE(pointInRing(Point2D{5, 5}, closed));
    REQUIRE_FALSE(pointInRing(Point2D{15, 5}, open));
    REQUIRE_FALSE(pointInRing(Point2D{15, 5}, closed));
    REQUIRE_FALSE(pointInRing(Point2D{5, -1}, open));

    // Degenerate rings can't contain anything.
    REQUIRE_FALSE(pointInRing(Point2D{0, 0}, Ring{Point2D{0, 0}, Point2D{1, 1}}));
}

TEST_CASE("distanceTo returns 0 inside a polygon and the boundary distance outside", "[predicates]") {
    const Geometry polygon = squareWithHole();

    REQUIRE(distanceTo(polygon, Point2D{1, 5}) == Approx(0.0));    // inside the ring proper
    REQUIRE(distanceTo(polygon, Point2D{-2, 5}) == Approx(2.0));   // outside, nearest left edge
    REQUIRE(distanceTo(polygon, Point2D{5, 13}) == Approx(3.0));   // outside, nearest top edge
}

TEST_CASE("distanceTo treats a polygon hole as outside", "[predicates]") {
    const Geometry polygon = squareWithHole();

    // Dead centre is inside the hole, so outside the polygon - and the
    // nearest boundary is the hole's edge (2.0 away), not the outer ring (5.0).
    REQUIRE(distanceTo(polygon, Point2D{5, 5}) == Approx(2.0));
    // Just inside the outer ring but outside the hole is still inside.
    REQUIRE(distanceTo(polygon, Point2D{2, 5}) == Approx(0.0));
}

TEST_CASE("distanceTo measures to the nearest segment of a line", "[predicates]") {
    Geometry line(GeometryType::LineString, {Part{Ring{Point2D{0, 0}, Point2D{10, 0}, Point2D{10, 10}}}});

    REQUIRE(distanceTo(line, Point2D{5, 4}) == Approx(4.0));   // above the first segment
    REQUIRE(distanceTo(line, Point2D{13, 5}) == Approx(3.0));  // right of the second segment
    REQUIRE(distanceTo(line, Point2D{10, 0}) == Approx(0.0));  // on the shared vertex

    // A line is not closed: the gap from (10,10) back to (0,0) is not an edge,
    // so a point near that imaginary line measures to a real segment instead.
    REQUIRE(distanceTo(line, Point2D{2, 4}) == Approx(4.0));
}

TEST_CASE("distanceTo measures to the nearest vertex of a point geometry", "[predicates]") {
    Geometry points(GeometryType::MultiPoint, {Part{Ring{Point2D{0, 0}, Point2D{10, 10}}}});

    REQUIRE(distanceTo(points, Point2D{3, 4}) == Approx(5.0));
    REQUIRE(distanceTo(points, Point2D{10, 10}) == Approx(0.0));
}

TEST_CASE("distanceTo returns infinity for empty and unknown geometry", "[predicates]") {
    // Never a hit, no matter how large the caller's tolerance.
    REQUIRE(std::isinf(distanceTo(Geometry{}, Point2D{0, 0})));
    REQUIRE(std::isinf(distanceTo(Geometry(GeometryType::Polygon, {}), Point2D{0, 0})));
}
