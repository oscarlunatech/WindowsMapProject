#include "cartograph/crs/transformer.h"

#include <proj.h>

#include <windows.h>

#include <cmath>

namespace cartograph::crs {

struct Transformer::Impl {
    PJ_CONTEXT* ctx = nullptr;
    PJ* transform = nullptr;  // null when identity == true
    bool identity = false;
    Envelope targetBounds;  // invalid when the target declares no area of use

    ~Impl() {
        if (transform != nullptr) {
            proj_destroy(transform);
        }
        if (ctx != nullptr) {
            proj_context_destroy(ctx);
        }
    }
};

namespace {
std::string lastError(PJ_CONTEXT* ctx) {
    const int err = proj_context_errno(ctx);
    const char* message = proj_context_errno_string(ctx, err);
    return message != nullptr ? message : "unknown PROJ error";
}

// proj.db ships next to the running executable (see the equivalent
// proj.db-copy comment in dataset.cpp), but a PJ_CONTEXT this module
// creates has no reason to already know that - unlike GDAL's own internal
// PROJ context, which dataset.cpp's ensurePlatformSetup() points at it via
// CPLSetConfigOption. This module doesn't touch GDAL/CPL at all (only
// dataset.cpp is allowed to), so it finds its own executable directory
// directly via Win32 and tells its own PJ_CONTEXT explicitly - keeps this
// module fully self-contained rather than implicitly depending on
// ensurePlatformSetup() having run first.
const std::string& executableDirectory() {
    static const std::string dir = [] {
        wchar_t pathBuffer[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, pathBuffer, MAX_PATH);
        if (length == 0 || length == MAX_PATH) {
            return std::string();
        }
        const int size =
            WideCharToMultiByte(CP_UTF8, 0, pathBuffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
        std::string path(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, pathBuffer, static_cast<int>(length), path.data(), size, nullptr, nullptr);
        const std::size_t lastSlash = path.find_last_of("\\/");
        return lastSlash == std::string::npos ? path : path.substr(0, lastSlash);
    }();
    return dir;
}

// The target CRS's area of use is published in degrees (lon/lat); this walks
// that box's perimeter through a WGS84 -> target transform to get the
// corresponding extent in target coordinates.
//
// The perimeter is sampled rather than just the four corners because a
// projection's edges bow: for a conic or transverse-Mercator target the
// extreme easting can fall at the middle of an edge, not at a corner, and
// four corners alone would clip real data. 32 samples per side is far more
// than enough at this precision, and it happens once per Transformer.
Envelope computeTargetBounds(PJ_CONTEXT* ctx, const std::string& targetCrs) {
    Envelope bounds;

    PJ* target = proj_create(ctx, targetCrs.c_str());
    if (target == nullptr) {
        return bounds;
    }
    double west = 0.0;
    double south = 0.0;
    double east = 0.0;
    double north = 0.0;
    const int haveArea =
        proj_get_area_of_use(ctx, target, &west, &south, &east, &north, nullptr);
    proj_destroy(target);
    if (haveArea == 0 || west > east || south > north) {
        return bounds;  // no declared area of use; nothing to clamp to
    }

    PJ* raw = proj_create_crs_to_crs(ctx, "EPSG:4326", targetCrs.c_str(), nullptr);
    if (raw == nullptr) {
        return bounds;
    }
    PJ* toTarget = proj_normalize_for_visualization(ctx, raw);
    proj_destroy(raw);
    if (toTarget == nullptr) {
        return bounds;
    }

    constexpr int kSamples = 32;
    const auto sample = [&](double lon, double lat) {
        const PJ_COORD out = proj_trans(toTarget, PJ_FWD, proj_coord(lon, lat, 0, 0));
        if (std::isfinite(out.xy.x) && std::isfinite(out.xy.y)) {
            bounds.expand(Point2D{out.xy.x, out.xy.y});
        }
    };
    for (int i = 0; i <= kSamples; ++i) {
        const double t = static_cast<double>(i) / kSamples;
        const double lon = west + (east - west) * t;
        const double lat = south + (north - south) * t;
        sample(lon, south);
        sample(lon, north);
        sample(west, lat);
        sample(east, lat);
    }

    proj_destroy(toTarget);
    return bounds;
}

double clampTo(double value, double low, double high) {
    return value < low ? low : (value > high ? high : value);
}
}  // namespace

Transformer::Transformer(const std::string& sourceCrsWkt, const std::string& targetCrs)
    : impl_(std::make_unique<Impl>()) {
    impl_->ctx = proj_context_create();
    // Real failures (malformed CRS, no pipeline found, axis-normalization
    // failure) are already surfaced as CrsError below via explicit nullptr
    // checks - PROJ's own stderr logging (e.g. "could not find required
    // grid(s)" when a high-accuracy datum-shift grid isn't installed, which
    // PROJ then gracefully degrades around per-pipeline-step rather than
    // failing outright) would otherwise print once per point transformed,
    // which is both noisy and, for a large dataset, a real perf cost.
    proj_log_level(impl_->ctx, PJ_LOG_NONE);
    if (const std::string& dir = executableDirectory(); !dir.empty()) {
        const char* searchPath = dir.c_str();
        proj_context_set_search_paths(impl_->ctx, 1, &searchPath);
    }

    PJ* source = proj_create(impl_->ctx, sourceCrsWkt.c_str());
    if (source == nullptr) {
        throw CrsError("failed to parse source CRS: " + lastError(impl_->ctx));
    }
    PJ* target = proj_create(impl_->ctx, targetCrs.c_str());
    if (target == nullptr) {
        const std::string error = lastError(impl_->ctx);
        proj_destroy(source);
        throw CrsError("failed to parse target CRS: " + error);
    }

    // Checked before building any transform pipeline: if source and target
    // are equivalent CRSs, transform() below is a passthrough rather than
    // running PROJ's pipeline - this is what keeps already-matching data
    // (e.g. the golden-image fixture, already EPSG:4326) bit-for-bit
    // unchanged rather than merely numerically very close.
    const bool equivalent = proj_is_equivalent_to(source, target, PJ_COMP_EQUIVALENT) != 0;
    proj_destroy(source);
    proj_destroy(target);

    if (equivalent) {
        impl_->identity = true;
        return;
    }

    PJ* raw = proj_create_crs_to_crs(impl_->ctx, sourceCrsWkt.c_str(), targetCrs.c_str(), nullptr);
    if (raw == nullptr) {
        throw CrsError("failed to create transform pipeline: " + lastError(impl_->ctx));
    }

    // proj_create_crs_to_crs's result uses each CRS's authority-defined axis
    // order, which for some CRSs is lat/lon rather than the conventional
    // GIS lon/lat - proj_normalize_for_visualization is PROJ's documented
    // fix, forcing (x, y) = (lon/easting, lat/northing) regardless.
    PJ* normalized = proj_normalize_for_visualization(impl_->ctx, raw);
    proj_destroy(raw);
    if (normalized == nullptr) {
        throw CrsError("failed to normalize transform axis order: " + lastError(impl_->ctx));
    }
    impl_->transform = normalized;
    impl_->targetBounds = computeTargetBounds(impl_->ctx, targetCrs);
}

Transformer::~Transformer() = default;
Transformer::Transformer(Transformer&&) noexcept = default;
Transformer& Transformer::operator=(Transformer&&) noexcept = default;

Point2D Transformer::transform(Point2D point) const {
    if (impl_->identity) {
        return point;
    }
    const PJ_COORD input = proj_coord(point.x, point.y, 0, 0);
    const PJ_COORD output = proj_trans(impl_->transform, PJ_FWD, input);

    // See targetBounds() in the header for why this clamp exists: PROJ
    // extrapolates far outside a projection's declared validity and reports
    // finite values while doing it, so nothing else would catch it.
    const Envelope& bounds = impl_->targetBounds;
    if (!bounds.valid) {
        return Point2D{output.xy.x, output.xy.y};
    }
    return Point2D{clampTo(output.xy.x, bounds.minX, bounds.maxX),
                    clampTo(output.xy.y, bounds.minY, bounds.maxY)};
}

bool Transformer::isIdentity() const { return impl_->identity; }

const Envelope& Transformer::targetBounds() const { return impl_->targetBounds; }

}  // namespace cartograph::crs
