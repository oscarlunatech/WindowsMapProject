#include <catch2/catch_test_macros.hpp>

#include "cartograph/geometry.h"

using namespace cartograph;

TEST_CASE("Envelope expands to cover points", "[geometry]") {
    Envelope env;
    env.expand(Point2D{1.0, 2.0});
    env.expand(Point2D{-1.0, 5.0});

    REQUIRE(env.valid);
    REQUIRE(env.minX == -1.0);
    REQUIRE(env.maxX == 1.0);
    REQUIRE(env.minY == 2.0);
    REQUIRE(env.maxY == 5.0);
}

TEST_CASE("Geometry extent covers all points across parts and rings", "[geometry]") {
    Geometry geom(GeometryType::MultiLineString, {
                                                       Part{Ring{Point2D{0, 0}, Point2D{10, 0}}},
                                                       Part{Ring{Point2D{5, -5}, Point2D{5, 20}}},
                                                   });

    const Envelope env = geom.extent();
    REQUIRE(env.valid);
    REQUIRE(env.minX == 0.0);
    REQUIRE(env.maxX == 10.0);
    REQUIRE(env.minY == -5.0);
    REQUIRE(env.maxY == 20.0);
}

TEST_CASE("Empty geometry has an invalid extent", "[geometry]") {
    Geometry geom;
    REQUIRE_FALSE(geom.extent().valid);
}
