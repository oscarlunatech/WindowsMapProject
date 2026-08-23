#pragma once

#include <stdexcept>
#include <string>

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "cartograph/dataset.h"
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
