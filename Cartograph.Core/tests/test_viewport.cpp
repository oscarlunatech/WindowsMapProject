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

TEST_CASE("a square extent on a wide screen fills the screen by widening the extent", "[viewport]") {
    // Map extent is square but the screen is wide (4:1), so rather than
    // pillarboxing, the displayed extent widens to 400x100 (also 4:1) about
    // the original center, and the square's corners land inside the screen
    // rather than at its edges.
    const Viewport viewport(makeExtent(0, 0, 100, 100), ScreenSize{400, 100});

    REQUIRE(viewport.mapExtent().minX == Approx(-150.0));
    REQUIRE(viewport.mapExtent().maxX == Approx(250.0));
    REQUIRE(viewport.mapExtent().minY == Approx(0.0));
    REQUIRE(viewport.mapExtent().maxY == Approx(100.0));
    REQUIRE(viewport.scale() == Approx(1.0));

    const Point2D bottomLeft = viewport.mapToScreen(Point2D{0, 0});
    const Point2D topRight = viewport.mapToScreen(Point2D{100, 100});

    REQUIRE(bottomLeft.x == Approx(150.0));
    REQUIRE(bottomLeft.y == Approx(100.0));
    REQUIRE(topRight.x == Approx(250.0));
    REQUIRE(topRight.y == Approx(0.0));

    // The screen's own corners now land exactly on the widened extent.
    const Point2D screenTopLeft = viewport.screenToMap(Point2D{0, 0});
    const Point2D screenBottomRight = viewport.screenToMap(Point2D{400, 100});
    REQUIRE(screenTopLeft.x == Approx(-150.0));
    REQUIRE(screenTopLeft.y == Approx(100.0));
    REQUIRE(screenBottomRight.x == Approx(250.0));
    REQUIRE(screenBottomRight.y == Approx(0.0));
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
