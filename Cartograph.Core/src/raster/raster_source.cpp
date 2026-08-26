#include "cartograph/raster/raster_source.h"

#include <gdal.h>
#include <gdal_alg.h>
#include <gdal_priv.h>
#include <gdalwarper.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <limits>
#include <mutex>

#include "cartograph/crs/transformer.h"
#include "gdal_platform.h"

namespace cartograph::raster {

namespace {

// Every RasterImage carries a version so the renderer can tell whether the
// bitmap it uploaded last frame is still current. Global rather than
// per-source so two sources can never hand out the same number.
std::atomic<std::uint64_t> g_nextVersion{1};

struct GeoTransform {
    double coefficients[6]{};

    double originX() const { return coefficients[0]; }
    double originY() const { return coefficients[3]; }
    double pixelWidth() const { return coefficients[1]; }
    double pixelHeight() const { return coefficients[5]; }  // normally negative: north-up
};

double clampTo(double value, double low, double high) {
    return value < low ? low : (value > high ? high : value);
}

}  // namespace

struct RasterSource::Impl {
    GDALDataset* owned = nullptr;   // the file as opened
    GDALDataset* warped = nullptr;  // warped VRT, or null when no warp was needed
    Envelope extent;
    GeoTransform transform;
    bool hasCrs = false;

    // A GDALDataset is not safe for concurrent access, and this class is
    // deliberately read from more than one thread: the viewer re-reads the
    // visible window on a background thread while identify samples pixels on
    // the UI thread. Both go through here, so both take this lock. Contention
    // is negligible - one is rare, the other is already I/O-bound.
    mutable std::mutex gdalMutex;

    // The dataset every read goes through: the warped VRT when there is one,
    // otherwise the file itself.
    GDALDataset* active() const { return warped != nullptr ? warped : owned; }

    ~Impl() {
        if (warped != nullptr) {
            GDALClose(warped);
        }
        if (owned != nullptr) {
            GDALClose(owned);
        }
    }
};

namespace {


Envelope extentOf(const GeoTransform& transform, int width, int height) {
    Envelope extent;
    // Corners rather than min/max arithmetic, so a south-up or otherwise
    // unusual geotransform still produces the right box.
    extent.expand(Point2D{transform.originX(), transform.originY()});
    extent.expand(Point2D{transform.originX() + transform.pixelWidth() * width,
                           transform.originY() + transform.pixelHeight() * height});
    return extent;
}

// Percentile bounds from the sampled values, using nth_element rather than a
// full sort - linear instead of n log n, and the exact order of the rest of
// the buffer doesn't matter.
void percentileBounds(std::vector<double>& scratch, double lowPct, double highPct, double& low,
                       double& high) {
    if (scratch.empty()) {
        low = 0.0;
        high = 1.0;
        return;
    }
    const auto index = [&](double pct) {
        const double clamped = clampTo(pct, 0.0, 100.0) / 100.0;
        const auto position = static_cast<std::size_t>(clamped * static_cast<double>(scratch.size() - 1));
        return (std::min)(position, scratch.size() - 1);
    };

    const std::size_t lowIndex = index(lowPct);
    std::nth_element(scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(lowIndex), scratch.end());
    low = scratch[lowIndex];

    const std::size_t highIndex = index(highPct);
    std::nth_element(scratch.begin(), scratch.begin() + static_cast<std::ptrdiff_t>(highIndex), scratch.end());
    high = scratch[highIndex];

    if (!(high > low)) {
        high = low + 1.0;  // a flat band would otherwise divide by zero
    }
}

// Builds the warped VRT with an **explicitly computed** output grid rather
// than letting GDALAutoCreateWarpedVRT pick one.
//
// This is the raster form of the trap Phase 10 found in the vector path, and
// it bites harder here. GDALAutoCreateWarpedVRT derives its output extent by
// transforming the source's corners itself, so a global geographic raster
// reaching latitude +/-90 produces a Web Mercator extent running to
// +/-242,000,000 - twelve times the projection's real bound. GDAL doesn't go
// through crs::Transformer, so that class's area-of-use clamp never applies.
// The result isn't merely a wrong extent: the output grid gets sized to cover
// it, so a 10800x5400 world image warps to something like 1119x12023 - almost
// all of it empty polar stretch, and a tenth of the real resolution.
//
// So: transform the source's own extent through crs::Transformer (which does
// clamp), and build the grid from that. Resolution is chosen to keep roughly
// the source's total pixel count, so reprojection doesn't quietly cost detail.
GDALDataset* createWarpedVrt(GDALDataset* source, const char* sourceWkt, const std::string& targetWkt,
                              const std::string& displayCrs) {
    double sourceTransformCoefficients[6]{};
    if (source->GetGeoTransform(sourceTransformCoefficients) != CE_None) {
        return nullptr;
    }
    GeoTransform sourceTransform;
    std::copy(std::begin(sourceTransformCoefficients), std::end(sourceTransformCoefficients),
              std::begin(sourceTransform.coefficients));

    const int sourceWidth = source->GetRasterXSize();
    const int sourceHeight = source->GetRasterYSize();
    const Envelope sourceExtent = extentOf(sourceTransform, sourceWidth, sourceHeight);

    // Sample the source extent's perimeter, not just its corners: a projected
    // edge bows, so the extreme easting or northing can fall mid-edge. Every
    // sample goes through crs::Transformer, which clamps to the target's
    // declared area of use.
    Envelope targetExtent;
    {
        const crs::Transformer toTarget(sourceWkt, displayCrs);
        constexpr int kSamples = 64;
        for (int i = 0; i <= kSamples; ++i) {
            const double t = static_cast<double>(i) / kSamples;
            const double x = sourceExtent.minX + sourceExtent.width() * t;
            const double y = sourceExtent.minY + sourceExtent.height() * t;
            targetExtent.expand(toTarget.transform(Point2D{x, sourceExtent.minY}));
            targetExtent.expand(toTarget.transform(Point2D{x, sourceExtent.maxY}));
            targetExtent.expand(toTarget.transform(Point2D{sourceExtent.minX, y}));
            targetExtent.expand(toTarget.transform(Point2D{sourceExtent.maxX, y}));
        }
    }
    if (!targetExtent.valid || targetExtent.width() <= 0.0 || targetExtent.height() <= 0.0) {
        return nullptr;
    }

    // Square pixels sized so the warped grid holds about as many pixels as the
    // source did.
    const double sourcePixels = static_cast<double>(sourceWidth) * static_cast<double>(sourceHeight);
    const double pixelSize = std::sqrt(targetExtent.width() * targetExtent.height() / sourcePixels);
    const int outWidth = (std::max)(1, static_cast<int>(std::lround(targetExtent.width() / pixelSize)));
    const int outHeight = (std::max)(1, static_cast<int>(std::lround(targetExtent.height() / pixelSize)));

    // North-up: origin at the top-left, negative pixel height.
    double targetTransform[6] = {targetExtent.minX, pixelSize, 0.0, targetExtent.maxY, 0.0, -pixelSize};

    GDALWarpOptions* options = GDALCreateWarpOptions();
    options->hSrcDS = source;
    // Bilinear rather than nearest-neighbour: this is a display path, and
    // nearest produces visible stair-stepping on reprojection.
    options->eResampleAlg = GRA_Bilinear;

    // GDALCreateWarpedVRT, unlike the GDALAutoCreateWarpedVRT convenience
    // wrapper, does not fill the band lists in for you - it validates and
    // fails with "nBandCount=0, no bands configured". GDALDestroyWarpOptions
    // frees these arrays.
    const int bandCount = source->GetRasterCount();
    options->nBandCount = bandCount;
    options->panSrcBands = static_cast<int*>(CPLMalloc(sizeof(int) * static_cast<std::size_t>(bandCount)));
    options->panDstBands = static_cast<int*>(CPLMalloc(sizeof(int) * static_cast<std::size_t>(bandCount)));
    for (int i = 0; i < bandCount; ++i) {
        options->panSrcBands[i] = i + 1;
        options->panDstBands[i] = i + 1;
    }
    options->pTransformerArg = GDALCreateGenImgProjTransformer3(sourceWkt, sourceTransformCoefficients,
                                                                 targetWkt.c_str(), targetTransform);
    if (options->pTransformerArg == nullptr) {
        GDALDestroyWarpOptions(options);
        return nullptr;
    }
    options->pfnTransformer = GDALGenImgProjTransform;

    auto* warped = static_cast<GDALDataset*>(
        GDALCreateWarpedVRT(source, outWidth, outHeight, targetTransform, options));
    GDALDestroyWarpOptions(options);
    return warped;
}

// Which bands feed red, green and blue. Automatic when the style names none:
// one band is grayscale, three or more take the first three.
std::vector<int> resolveBands(const RasterStyle& style, int bandCount) {
    if (!style.bands.empty()) {
        std::vector<int> bands;
        for (const int band : style.bands) {
            if (band >= 1 && band <= bandCount) {
                bands.push_back(band);
            }
        }
        if (!bands.empty()) {
            return bands;
        }
    }
    if (bandCount >= 3) {
        return {1, 2, 3};
    }
    return {1};
}

// Resolves StretchMode::Automatic against what the file actually is: an image
// whose bands are all 8-bit and tagged red/green/blue is already display-ready
// and gets left alone; anything else gets a percentile stretch.
StretchMode resolveStretch(StretchMode requested, GDALDataset* dataset, const std::vector<int>& bands) {
    if (requested != StretchMode::Automatic) {
        return requested;
    }
    if (bands.size() != 3) {
        return StretchMode::Percentile;  // single band: almost certainly data, not colour
    }
    for (const int band : bands) {
        GDALRasterBand* raster = dataset->GetRasterBand(band);
        if (raster == nullptr || raster->GetRasterDataType() != GDT_Byte) {
            return StretchMode::Percentile;
        }
        const GDALColorInterp interpretation = raster->GetColorInterpretation();
        if (interpretation != GCI_RedBand && interpretation != GCI_GreenBand &&
            interpretation != GCI_BlueBand) {
            return StretchMode::Percentile;
        }
    }
    return StretchMode::None;
}

}  // namespace

RasterSource::RasterSource(const std::string& path, const std::string& displayCrs)
    : impl_(std::make_unique<Impl>()) {
    detail::ensureGdalPlatformSetup();

    impl_->owned = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (impl_->owned == nullptr) {
        throw RasterError("failed to open raster '" + path + "'");
    }
    if (impl_->owned->GetRasterCount() < 1) {
        throw RasterError("raster '" + path + "' has no bands");
    }

    const char* sourceWkt = impl_->owned->GetProjectionRef();
    impl_->hasCrs = sourceWkt != nullptr && sourceWkt[0] != '\0';

    if (impl_->hasCrs && !displayCrs.empty()) {
        OGRSpatialReference source;
        OGRSpatialReference target;
        if (source.SetFromUserInput(sourceWkt) != OGRERR_NONE) {
            throw RasterError("raster '" + path + "' has a CRS PROJ can't parse");
        }
        if (target.SetFromUserInput(displayCrs.c_str()) != OGRERR_NONE) {
            throw RasterError("failed to parse display CRS '" + displayCrs + "'");
        }

        // Skip the warp entirely when the raster is already in the display CRS
        // - same reasoning as crs::Transformer's identity short-circuit, and it
        // keeps a matching raster pixel-exact rather than resampled.
        if (!source.IsSame(&target)) {
            char* targetWkt = nullptr;
            target.exportToWkt(&targetWkt);
            const std::string targetWktOwned = targetWkt != nullptr ? targetWkt : std::string();
            CPLFree(targetWkt);

            impl_->warped = createWarpedVrt(impl_->owned, sourceWkt, targetWktOwned, displayCrs);
            if (impl_->warped == nullptr) {
                throw RasterError("failed to warp raster '" + path + "' into " + displayCrs);
            }
        }
    }

    GDALDataset* active = impl_->active();
    if (active->GetGeoTransform(impl_->transform.coefficients) != CE_None) {
        // No geotransform: GDAL's default maps pixel space 1:1, which at least
        // renders something sensible for an unreferenced image.
        impl_->transform.coefficients[0] = 0.0;
        impl_->transform.coefficients[1] = 1.0;
        impl_->transform.coefficients[2] = 0.0;
        impl_->transform.coefficients[3] = 0.0;
        impl_->transform.coefficients[4] = 0.0;
        impl_->transform.coefficients[5] = 1.0;
    }
    impl_->extent = extentOf(impl_->transform, active->GetRasterXSize(), active->GetRasterYSize());
}

RasterSource::~RasterSource() = default;
RasterSource::RasterSource(RasterSource&&) noexcept = default;
RasterSource& RasterSource::operator=(RasterSource&&) noexcept = default;

const Envelope& RasterSource::extent() const { return impl_->extent; }
int RasterSource::bandCount() const { return impl_->active()->GetRasterCount(); }
int RasterSource::width() const { return impl_->active()->GetRasterXSize(); }
int RasterSource::height() const { return impl_->active()->GetRasterYSize(); }
bool RasterSource::hasCrs() const { return impl_->hasCrs; }

RasterImage RasterSource::read(const Envelope& window, int maxWidth, int maxHeight,
                                const RasterStyle& style) const {
    const std::lock_guard<std::mutex> lock(impl_->gdalMutex);
    RasterImage image;
    if (maxWidth <= 0 || maxHeight <= 0 || !impl_->extent.valid) {
        return image;
    }

    // Only the part of the window that actually overlaps the raster.
    const Envelope& full = impl_->extent;
    const double minX = (std::max)(window.minX, full.minX);
    const double maxX = (std::min)(window.maxX, full.maxX);
    const double minY = (std::max)(window.minY, full.minY);
    const double maxY = (std::min)(window.maxY, full.maxY);
    if (!(maxX > minX) || !(maxY > minY)) {
        return image;  // no overlap
    }

    GDALDataset* active = impl_->active();
    const GeoTransform& transform = impl_->transform;

    // Map coordinates to pixel coordinates. pixelHeight is normally negative
    // (north-up), so the map's maxY is the raster's top row.
    const double pixelsPerX = 1.0 / transform.pixelWidth();
    const double pixelsPerY = 1.0 / transform.pixelHeight();
    double left = (minX - transform.originX()) * pixelsPerX;
    double right = (maxX - transform.originX()) * pixelsPerX;
    double top = (maxY - transform.originY()) * pixelsPerY;
    double bottom = (minY - transform.originY()) * pixelsPerY;
    if (left > right) {
        std::swap(left, right);
    }
    if (top > bottom) {
        std::swap(top, bottom);
    }

    const int srcX = (std::max)(0, static_cast<int>(std::floor(left)));
    const int srcY = (std::max)(0, static_cast<int>(std::floor(top)));
    const int srcRight = (std::min)(active->GetRasterXSize(), static_cast<int>(std::ceil(right)));
    const int srcBottom = (std::min)(active->GetRasterYSize(), static_cast<int>(std::ceil(bottom)));
    const int srcWidth = srcRight - srcX;
    const int srcHeight = srcBottom - srcY;
    if (srcWidth <= 0 || srcHeight <= 0) {
        return image;
    }

    // Never read more pixels than can be shown. GDAL picks a matching overview
    // level for a downsampled request by itself, which is what keeps this fast
    // on pyramided files.
    const double scale =
        (std::min)(1.0, (std::min)(static_cast<double>(maxWidth) / srcWidth,
                                     static_cast<double>(maxHeight) / srcHeight));
    const int outWidth = (std::max)(1, static_cast<int>(std::lround(srcWidth * scale)));
    const int outHeight = (std::max)(1, static_cast<int>(std::lround(srcHeight * scale)));

    const std::vector<int> bands = resolveBands(style, active->GetRasterCount());
    const StretchMode stretch = resolveStretch(style.stretch, active, bands);
    const std::size_t pixelCount = static_cast<std::size_t>(outWidth) * outHeight;

    // Read every band as double so one code path covers Byte, Int16, Float32
    // and the rest; the buffers are bounded by the output size, not the file.
    std::vector<std::vector<double>> planes(bands.size());
    for (std::size_t i = 0; i < bands.size(); ++i) {
        planes[i].resize(pixelCount);
        GDALRasterBand* band = active->GetRasterBand(bands[i]);
        if (band == nullptr) {
            return image;
        }
        if (band->RasterIO(GF_Read, srcX, srcY, srcWidth, srcHeight, planes[i].data(), outWidth, outHeight,
                            GDT_Float64, 0, 0) != CE_None) {
            throw RasterError("failed to read raster window");
        }
    }

    // An explicit alpha band, if the file has one, becomes real transparency.
    std::vector<double> alphaPlane;
    for (int b = 1; b <= active->GetRasterCount(); ++b) {
        GDALRasterBand* band = active->GetRasterBand(b);
        if (band != nullptr && band->GetColorInterpretation() == GCI_AlphaBand) {
            alphaPlane.resize(pixelCount);
            if (band->RasterIO(GF_Read, srcX, srcY, srcWidth, srcHeight, alphaPlane.data(), outWidth,
                                outHeight, GDT_Float64, 0, 0) != CE_None) {
                alphaPlane.clear();
            }
            break;
        }
    }

    // Nodata pixels drop out rather than painting a block of whatever sentinel
    // value the file uses.
    std::vector<double> noData(bands.size(), std::numeric_limits<double>::quiet_NaN());
    for (std::size_t i = 0; i < bands.size(); ++i) {
        int hasNoData = 0;
        const double value = active->GetRasterBand(bands[i])->GetNoDataValue(&hasNoData);
        if (hasNoData != 0) {
            noData[i] = value;
        }
    }

    // Stretch bounds per band, from the pixels actually being shown - so
    // zooming into a dark corner brings out its detail rather than keeping the
    // whole-file contrast.
    std::vector<double> low(bands.size(), 0.0);
    std::vector<double> high(bands.size(), 255.0);
    if (stretch != StretchMode::None) {
        for (std::size_t i = 0; i < bands.size(); ++i) {
            std::vector<double> scratch;
            scratch.reserve(planes[i].size());
            for (const double value : planes[i]) {
                if (std::isfinite(value) && !(std::isfinite(noData[i]) && value == noData[i])) {
                    scratch.push_back(value);
                }
            }
            if (stretch == StretchMode::MinMax) {
                percentileBounds(scratch, 0.0, 100.0, low[i], high[i]);
            } else {
                percentileBounds(scratch, style.stretchLow, style.stretchHigh, low[i], high[i]);
            }
        }
    }

    const auto toByte = [&](double value, std::size_t bandIndex) -> std::uint8_t {
        if (stretch == StretchMode::None) {
            return static_cast<std::uint8_t>(clampTo(value, 0.0, 255.0));
        }
        const double t = (value - low[bandIndex]) / (high[bandIndex] - low[bandIndex]);
        return static_cast<std::uint8_t>(clampTo(t * 255.0, 0.0, 255.0));
    };

    image.width = outWidth;
    image.height = outHeight;
    image.bgra.resize(pixelCount * 4);
    image.version = g_nextVersion.fetch_add(1);

    // The extent of what was actually read, in map coordinates - the pixel
    // window was snapped outward to whole pixels, so this is not identical to
    // the requested overlap and the renderer must place the image by *this*.
    Envelope readExtent;
    readExtent.expand(Point2D{transform.originX() + srcX * transform.pixelWidth(),
                               transform.originY() + srcY * transform.pixelHeight()});
    readExtent.expand(Point2D{transform.originX() + srcRight * transform.pixelWidth(),
                               transform.originY() + srcBottom * transform.pixelHeight()});
    image.extent = readExtent;

    for (std::size_t p = 0; p < pixelCount; ++p) {
        std::uint8_t red = 0;
        std::uint8_t green = 0;
        std::uint8_t blue = 0;
        bool transparent = false;

        if (bands.size() >= 3) {
            red = toByte(planes[0][p], 0);
            green = toByte(planes[1][p], 1);
            blue = toByte(planes[2][p], 2);
            transparent = (std::isfinite(noData[0]) && planes[0][p] == noData[0]);
        } else {
            const std::uint8_t gray = toByte(planes[0][p], 0);
            red = green = blue = gray;
            transparent = (std::isfinite(noData[0]) && planes[0][p] == noData[0]);
        }

        std::uint8_t alpha = transparent ? 0 : 255;
        if (!alphaPlane.empty()) {
            alpha = static_cast<std::uint8_t>(clampTo(alphaPlane[p], 0.0, 255.0));
        }

        // Premultiplied BGRA, matching the render targets' pixel format.
        const double a = alpha / 255.0;
        std::uint8_t* out = image.bgra.data() + p * 4;
        out[0] = static_cast<std::uint8_t>(blue * a);
        out[1] = static_cast<std::uint8_t>(green * a);
        out[2] = static_cast<std::uint8_t>(red * a);
        out[3] = alpha;
    }

    return image;
}

std::vector<double> RasterSource::sample(Point2D mapPoint) const {
    const std::lock_guard<std::mutex> lock(impl_->gdalMutex);
    std::vector<double> values;
    const Envelope& full = impl_->extent;
    if (!full.valid || mapPoint.x < full.minX || mapPoint.x > full.maxX || mapPoint.y < full.minY ||
        mapPoint.y > full.maxY) {
        return values;
    }

    GDALDataset* active = impl_->active();
    const GeoTransform& transform = impl_->transform;
    const int px = static_cast<int>(std::floor((mapPoint.x - transform.originX()) / transform.pixelWidth()));
    const int py = static_cast<int>(std::floor((mapPoint.y - transform.originY()) / transform.pixelHeight()));
    if (px < 0 || py < 0 || px >= active->GetRasterXSize() || py >= active->GetRasterYSize()) {
        return values;
    }

    for (int b = 1; b <= active->GetRasterCount(); ++b) {
        double value = 0.0;
        if (active->GetRasterBand(b)->RasterIO(GF_Read, px, py, 1, 1, &value, 1, 1, GDT_Float64, 0, 0) ==
            CE_None) {
            values.push_back(value);
        }
    }
    return values;
}

}  // namespace cartograph::raster
