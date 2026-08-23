#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "cartograph/render/viewport.h"

using namespace cartograph;
using namespace cartograph::render;
using Catch::Approx;

namespace {
Envelope makeExtent(double minX, double minY, double maxX, double maxY) {
    Envelope e;
    e.expand(Point2D{minX, minY});
    e.expand(Point2D{maxX, maxY});
    return e;
}
}  // namespace

TEST_CASE("mapToScreen places the extent corners at the screen corners", "[viewport]") {
    // A square map extent fit into a square screen needs no letterboxing.
    const Viewport viewport(makeExtent(0, 0, 100, 100), ScreenSize{200, 200});

    const Point2D bottomLeft = viewport.mapToScreen(Point2D{0, 0});
    REQUIRE(bottomLeft.x == Approx(0.0));
    REQUIRE(bottomLeft.y == Approx(200.0));  // Y flips: map-south is screen-bottom

    const Point2D topRight = viewport.mapToScreen(Point2D{100, 100});
    REQUIRE(topRight.x == Approx(200.0));
    REQUIRE(topRight.y == Approx(0.0));
}

TEST_CASE("aspect ratio is preserved by pillarboxing a square extent on a wide screen", "[viewport]") {
    // Map extent is square but the screen is wide, so height is the limiting
    // dimension: the square should render at its natural 100x100 size,
    // centered with equal blank margins on left and right.
    const Viewport viewport(makeExtent(0, 0, 100, 100), ScreenSize{400, 100});

    const Point2D bottomLeft = viewport.mapToScreen(Point2D{0, 0});
    const Point2D topRight = viewport.mapToScreen(Point2D{100, 100});

    // Drawn square is 100x100 (scale=1, limited by height), centered in a
    // 400-wide screen: margins of 150 on each side.
    REQUIRE(bottomLeft.x == Approx(150.0));
    REQUIRE(bottomLeft.y == Approx(100.0));
    REQUIRE(topRight.x == Approx(250.0));
    REQUIRE(topRight.y == Approx(0.0));
}

TEST_CASE("screenToMap is the inverse of mapToScreen", "[viewport]") {
    const Viewport viewport(makeExtent(-180, -90, 180, 90), ScreenSize{800, 400});

    for (const Point2D original : {Point2D{-180, -90}, Point2D{0, 0}, Point2D{45.5, -12.25},
                                    Point2D{179.9, 89.9}}) {
        const Point2D roundTripped = viewport.screenToMap(viewport.mapToScreen(original));
        REQUIRE(roundTripped.x == Approx(original.x).margin(1e-9));
        REQUIRE(roundTripped.y == Approx(original.y).margin(1e-9));
    }
}
