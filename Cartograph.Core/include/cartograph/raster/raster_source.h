#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "cartograph/geometry.h"

namespace cartograph::raster {

class RasterError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class StretchMode {
    // Pick per file: none for an image that is already display-ready (8-bit
    // bands tagged red/green/blue - an aerial photo, a shaded-relief basemap),
    // percentile for anything else (a DEM, 16-bit satellite bands, an
    // untagged single band).
    //
    // The distinction matters: percentile-stretching an RGB basemap visibly
    // oversaturates it, because its colours were already correct and the
    // stretch re-maps them to whatever happens to be in view. Stretching a DEM
    // is the only way to see it at all, since raw elevations are not pixel
    // values. Neither default is right for both, so this asks the file.
    Automatic,
    None,        // raw values, clipped to 0-255
    MinMax,      // full range of the window stretched across 0-255
    Percentile,  // as MinMax but ignoring outliers at each end
};

// How a raster's bands become screen pixels.
struct RasterStyle {
    // 1-based band numbers, in red/green/blue order. Empty means automatic:
    // one band renders as grayscale, three or more take the first three as
    // RGB. A single entry is always grayscale.
    std::vector<int> bands;

    StretchMode stretch = StretchMode::Automatic;
    double stretchLow = 2.0;   // percentile cut at the dark end
    double stretchHigh = 98.0;  // percentile cut at the bright end

    friend bool operator==(const RasterStyle&, const RasterStyle&) = default;
};

// One decoded image covering `extent`, ready to blit.
//
// Deliberately plain data - no GDAL handle, no D2D resource - so it can be
// produced on a worker thread and handed to the UI thread, which is the whole
// point: reading a window is I/O and must not happen in the paint path.
//
// bgra is premultiplied BGRA, matching the render targets' pixel format so the
// renderer can hand it straight to CreateBitmap with no conversion.
struct RasterImage {
    int width = 0;
    int height = 0;
    Envelope extent;  // the area actually covered, which may be smaller than requested
    std::vector<std::uint8_t> bgra;

    // Bumped every time a source produces a new image. The renderer keys its
    // cached ID2D1Bitmap on this, so an unchanged image isn't re-uploaded to
    // the GPU every frame.
    std::uint64_t version = 0;

    bool empty() const { return width <= 0 || height <= 0; }
};

// A raster file, warped into a display CRS, that can be read window by window.
//
// Unlike Dataset (which converts everything on ingest and closes GDAL before
// returning), a RasterSource **keeps its GDAL handle open** for its lifetime.
// That's required by design: Phase 11 re-reads the visible window at screen
// resolution whenever the view changes, so there is no single "load it all"
// moment. GDAL stays hidden inside raster_source.cpp behind this pimpl.
class RasterSource {
public:
    // Opens path and, if its CRS differs from displayCrs, wraps it in a warped
    // VRT so every subsequent read comes back already reprojected - the same
    // "everything lives in one coordinate space" invariant vector layers get
    // from Dataset::open. Throws RasterError if the file can't be opened, has
    // no bands, or can't be warped.
    RasterSource(const std::string& path, const std::string& displayCrs);
    ~RasterSource();

    RasterSource(const RasterSource&) = delete;
    RasterSource& operator=(const RasterSource&) = delete;
    RasterSource(RasterSource&&) noexcept;
    RasterSource& operator=(RasterSource&&) noexcept;

    // Full extent in the display CRS.
    const Envelope& extent() const;

    int bandCount() const;
    int width() const;   // pixels across the warped raster
    int height() const;

    // Whether the file carried CRS metadata at all. A raster without it is
    // passed through unwarped and its extent is in whatever units the
    // geotransform used, exactly as an unreferenced vector layer is.
    bool hasCrs() const;

    // Reads the part of `window` that overlaps this raster, downsampled to at
    // most maxWidth x maxHeight. GDAL picks an appropriate overview level for
    // the requested size on its own, which is what makes this cheap on
    // pyramided files and merely slow on files without overviews.
    //
    // The returned image's extent is the *overlap*, not the requested window,
    // so a partially off-screen raster still draws in the right place. Returns
    // an empty image when the window misses the raster entirely.
    RasterImage read(const Envelope& window, int maxWidth, int maxHeight, const RasterStyle& style) const;

    // Raw (unstretched, unscaled) value of every band at one map coordinate -
    // what identify reports. Empty if the point falls outside the raster.
    std::vector<double> sample(Point2D mapPoint) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cartograph::raster
