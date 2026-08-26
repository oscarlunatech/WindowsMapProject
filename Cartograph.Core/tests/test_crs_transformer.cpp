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

TEST_CASE("Web Mercator clamps to its declared area of use", "[crs]") {
    // PROJ itself does not clamp: without this, latitude -90 transforms to
    // y = -242,529,000, twelve times EPSG:3857's own southern bound, and
    // finite - so nothing errors, the data just becomes unusable and any
    // dataset containing Antarctica blows the map extent up by 12x.
    const Transformer t("EPSG:4326", "EPSG:3857");
    REQUIRE_FALSE(t.isIdentity());

    const Envelope& bounds = t.targetBounds();
    REQUIRE(bounds.valid);

    // The canonical Web Mercator bound is 20,037,508 m, at latitude
    // 85.0511288 (the latitude that makes the world exactly square). PROJ
    // reports the *EPSG registry's* area of use instead, which is rounded to
    // 85.06 - and near the pole a degree of latitude is ~1,276 km of
    // northing, so that 0.0089-degree rounding is ~11 km, putting the bound
    // at ~20,048,966. Deliberately not corrected to the canonical constant:
    // the clamp's job is to stop PROJ extrapolating to 242,000,000, and
    // 0.06% past the square is irrelevant to that. Asserted loosely so this
    // doesn't break if a PROJ update refines the registry value.
    REQUIRE(bounds.maxY == Catch::Approx(20037508.0).margin(20000.0));
    REQUIRE(bounds.minY == Catch::Approx(-20037508.0).margin(20000.0));

    const Point2D pole = t.transform(Point2D{10.0, -90.0});
    REQUIRE(pole.y == Catch::Approx(bounds.minY));

    // Inside the area of use the clamp must not touch anything.
    const Point2D mid = t.transform(Point2D{10.0, 45.0});
    REQUIRE(mid.y == Catch::Approx(5621521.0).margin(100.0));
}

TEST_CASE("An identity transform clamps nothing", "[crs]") {
    // Same-CRS transforms short-circuit before any bounds are computed, which
    // is what keeps already-EPSG:4326 data (the golden-image fixture) exactly
    // untouched.
    const Transformer t("EPSG:4326", "EPSG:4326");
    REQUIRE(t.isIdentity());
    REQUIRE_FALSE(t.targetBounds().valid);

    const Point2D pole{10.0, -90.0};
    REQUIRE(t.transform(pole).x == pole.x);
    REQUIRE(t.transform(pole).y == pole.y);
}
