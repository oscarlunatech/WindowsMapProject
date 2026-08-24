#include "cartograph/crs/transformer.h"

#include <proj.h>

#include <windows.h>

namespace cartograph::crs {

struct Transformer::Impl {
    PJ_CONTEXT* ctx = nullptr;
    PJ* transform = nullptr;  // null when identity == true
    bool identity = false;

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
    return Point2D{output.xy.x, output.xy.y};
}

bool Transformer::isIdentity() const { return impl_->identity; }

}  // namespace cartograph::crs
