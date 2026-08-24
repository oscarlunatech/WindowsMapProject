#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "cartograph/dataset.h"
#include "cartograph/jobs/thread_pool.h"
#include "cartograph/render/layer_cache.h"
#include "cartograph/render/viewport.h"

namespace cartograph::render {

class RenderError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Draws every feature of the dataset into an already-open render target
// (the caller owns BeginDraw/EndDraw) using the given viewport. Shared by
// Renderer::render (off-screen WIC target) and the live window
// (HwndRenderTarget), so both draw identically. Styling is a hardcoded
// placeholder until Phase 7 introduces real symbology.
void drawDataset(ID2D1RenderTarget& target, ID2D1Factory& factory, const Dataset& dataset,
                  const Viewport& viewport);

// Like drawDataset, but culls each layer against its LayerCache's spatial
// index (only features intersecting the viewport are drawn), draws
// per-zoom-level-simplified geometry instead of the original, and batches
// every visible line/polygon per layer into one ID2D1PathGeometry each
// instead of one draw call per feature. layerCaches must be the same size
// as dataset.layers(), one entry per layer in order. Returns the total
// number of features actually drawn (for a "features drawn"/"culled"
// overlay - see Viewer::onPaint).
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
std::size_t drawDatasetCulled(ID2D1RenderTarget& target, ID2D1Factory& factory, const Dataset& dataset,
                               const std::vector<LayerCache>& layerCaches, const Viewport& viewport,
                               jobs::ThreadPool& pool);

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
    // Renders every layer of the dataset into an off-screen bitmap using the
    // given viewport. Forces the D2D software (WARP) rasterizer so output
    // doesn't depend on the local GPU/driver - this is what makes
    // golden-image comparisons reproducible.
    static Microsoft::WRL::ComPtr<IWICBitmap> render(const Dataset& dataset, const Viewport& viewport);
};

// Encodes a 32bpp WIC bitmap to a PNG file. Throws RenderError on failure.
void savePng(IWICBitmap* bitmap, const std::string& path);

}  // namespace cartograph::render
