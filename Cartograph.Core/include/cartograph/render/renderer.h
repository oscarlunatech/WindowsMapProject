#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "cartograph/jobs/thread_pool.h"
#include "cartograph/map.h"
#include "cartograph/render/layer_cache.h"
#include "cartograph/render/viewport.h"
#include "cartograph/style/stylesheet.h"

namespace cartograph::render {

class RenderError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Re-reads every visible raster layer's window for this viewport and stores
// the result on the layer, so a following drawMap/drawMapCulled has current
// pixels to blit.
//
// Synchronous, and does file I/O - the caller chooses the thread. One-shot
// commands (render, bench) call it inline; the live viewer calls it on a
// background thread and hands the images back to the UI thread, because a read
// must never happen in the paint path.
//
// Reads at the viewport's own pixel size, so a raster stays sharp at every
// zoom rather than being scaled up from one load-time bitmap. GDAL selects a
// matching overview level for the downsampled request by itself, which is what
// keeps this cheap on pyramided files.
void refreshRasterLayers(Map& map, const Viewport& viewport, const style::Stylesheet& stylesheet);

// Draws every feature of every visible layer into an already-open render
// target (the caller owns BeginDraw/EndDraw) using the given viewport. Layers
// draw bottom-to-top in map.layers() order, so later layers land on top.
// Shared by Renderer::render (off-screen WIC target) and the live window
// (HwndRenderTarget), so both draw identically.
//
// stylesheet must have been built against this same map (see
// style::Stylesheet); throws RenderError if its layer count disagrees. The
// overload without one styles every feature with style::Symbol's defaults,
// which are exactly the colors this function hardcoded before Phase 7 - it
// builds a default Stylesheet per call, so it's for one-shot rendering, not
// a per-frame loop.
void drawMap(ID2D1RenderTarget& target, ID2D1Factory& factory, const Map& map, const Viewport& viewport,
              const style::Stylesheet& stylesheet);
void drawMap(ID2D1RenderTarget& target, ID2D1Factory& factory, const Map& map, const Viewport& viewport);

// Like drawMap, but culls each layer against its LayerCache's spatial index
// (only features intersecting the viewport are drawn), draws
// per-zoom-level-simplified geometry instead of the original, and batches
// visible lines/polygons into one ID2D1PathGeometry and one draw call each
// instead of one per feature. Each layer carries its own cache, so there's
// nothing to keep in sync. Returns the total number of features actually
// drawn (for a "features drawn"/"culled" overlay - see Viewer::onPaint).
//
// Hidden layers are skipped, and a layer's opacity is applied by setting it
// on the brushes for the duration of that layer.
//
// Batching is per (layer, symbol) rather than per layer: a layer's visible
// features are grouped by the symbol stylesheet resolves them to, and each
// group gets its own batched geometry. A single-symbol layer therefore
// collapses to exactly one group and batches identically to Phase 4, while a
// categorized layer costs one batch per category actually on screen - not one
// per feature. Grouping runs in the parallel phase and is a plain array
// lookup, since Stylesheet resolved every feature's symbol once at
// construction.
//
// stylesheet is a required parameter rather than defaulted because this is
// the per-frame path: build it once when the map loads (style::Stylesheet's
// constructor is O(features)) and pass the same one every frame. Throws
// RenderError if it wasn't built against this map.
//
// Each layer's spatial-index query, simplification-bucket lookup,
// map-to-screen coordinate transform, AND ID2D1PathGeometry construction
// (the sink-writing itself) run in parallel across pool; only the final
// DrawGeometry/FillGeometry/FillEllipse submission onto target happens
// serially on the calling thread, since ID2D1RenderTarget stays
// thread-affine no matter the factory's threading mode. This requires
// factory to have been created D2D1_FACTORY_TYPE_MULTI_THREADED (not
// SINGLE_THREADED) - that's what makes concurrent resource creation across
// pool workers safe.
std::size_t drawMapCulled(ID2D1RenderTarget& target, ID2D1Factory& factory, const Map& map,
                           const Viewport& viewport, jobs::ThreadPool& pool,
                           const style::Stylesheet& stylesheet);

// A reusable off-screen D2D render target backed by a WIC bitmap, forcing
// the software (WARP) rasterizer for reproducibility. Exists so repeated
// draws (e.g. the benchmark harness timing many frames) pay render-target
// setup cost once, not per frame - Renderer::render is a thin wrapper
// around a single beginFrame/drawDataset/endFrame cycle.
class OffscreenTarget {
public:
    explicit OffscreenTarget(ScreenSize size);

    ID2D1RenderTarget& renderTarget() { return *renderTarget_.Get(); }
    ID2D1Factory& factory() { return *d2dFactory_.Get(); }
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap() const { return wicBitmap_; }

    void beginFrame();
    void endFrame();  // throws RenderError if EndDraw fails

private:
    Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap_;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> renderTarget_;
};

class Renderer {
public:
    // Renders every visible layer of the map into an off-screen bitmap using
    // the given viewport. Forces the D2D software (WARP) rasterizer so output
    // doesn't depend on the local GPU/driver - this is what makes
    // golden-image comparisons reproducible. The overload without a
    // stylesheet uses style::Symbol's defaults, i.e. the exact styling that
    // predates Phase 7.
    static Microsoft::WRL::ComPtr<IWICBitmap> render(const Map& map, const Viewport& viewport);
    static Microsoft::WRL::ComPtr<IWICBitmap> render(const Map& map, const Viewport& viewport,
                                                     const style::Stylesheet& stylesheet);
};

// Encodes a 32bpp WIC bitmap to a PNG file. Throws RenderError on failure.
void savePng(IWICBitmap* bitmap, const std::string& path);

}  // namespace cartograph::render
