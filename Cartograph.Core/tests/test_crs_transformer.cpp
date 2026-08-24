#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "cartograph/crs/transformer.h"

using namespace cartograph;
using namespace cartograph::crs;
using Catch::Approx;

TEST_CASE("identical CRSs are an identity transform", "[crs]") {
    const Transformer t("EPSG:4326", "EPSG:4326");
    REQUIRE(t.isIdentity());

    const Point2D p{-74.19, 40.74};
    const Point2D result = t.transform(p);
    REQUIRE(result.x == p.x);
    REQUIRE(result.y == p.y);
}

TEST_CASE("a real transform maps the WGS84/Web Mercator shared origin exactly", "[crs]") {
    const Transformer t("EPSG:4326", "EPSG:3857");
    REQUIRE_FALSE(t.isIdentity());

    const Point2D result = t.transform(Point2D{0.0, 0.0});
    REQUIRE(result.x == Approx(0.0).margin(1e-9));
    REQUIRE(result.y == Approx(0.0).margin(1e-9));
}

TEST_CASE("round-tripping through a real transform recovers the original point", "[crs]") {
    const Transformer forward("EPSG:4326", "EPSG:3857");
    const Transformer backward("EPSG:3857", "EPSG:4326");

    const Point2D original{-74.19, 40.74};
    const Point2D roundTripped = backward.transform(forward.transform(original));

    REQUIRE(roundTripped.x == Approx(original.x).margin(1e-7));
    REQUIRE(roundTripped.y == Approx(original.y).margin(1e-7));
}

TEST_CASE("a malformed CRS string throws CrsError", "[crs]") {
    REQUIRE_THROWS_AS(Transformer("not a real CRS", "EPSG:4326"), CrsError);
}

TEST_CASE("NAD83 to WGS84 (the real NJ roads case) degrades gracefully without a NADCON grid installed",
          "[crs]") {
    // This vcpkg PROJ install has no NADCON5/NTv2 grid files, so PROJ's
    // "best" NAD83->WGS84 operation (which needs one) can't fully execute -
    // it prints a warning (suppressed - see Transformer's PJ_LOG_NONE) and
    // gracefully degrades that step to identity, per PROJ's own documented
    // behavior for a missing grid within an otherwise-valid pipeline. For
    // this specific pair the result is a well-known, accepted
    // approximation (NAD83 and WGS84 differ by roughly 1-2m in CONUS - see
    // the Phase 6 DECISIONS.md entry) - not NaN/garbage, and not silently
    // treated as a *different*, wrong CRS. Documented here as the concrete
    // case Phase 6 accepted rather than solving with network grid fetching
    // or bundled grid files.
    const Transformer t("EPSG:4269", "EPSG:4326");
    REQUIRE_FALSE(t.isIdentity());

    const Point2D original{-74.19, 40.74};
    const Point2D result = t.transform(original);
    REQUIRE(std::isfinite(result.x));
    REQUIRE(std::isfinite(result.y));
    // Degrades to an unshifted passthrough for this specific pair/hardware -
    // if a future PROJ/vcpkg update ships the grid, this would start
    // failing (result would differ from `original` by the true ~1-2m
    // datum shift), which is a reasonable trigger to revisit this test.
    REQUIRE(result.x == original.x);
    REQUIRE(result.y == original.y);
}
