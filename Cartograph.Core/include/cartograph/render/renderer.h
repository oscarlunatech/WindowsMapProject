#pragma once

#include <stdexcept>
#include <string>

#include <wincodec.h>
#include <wrl/client.h>

#include "cartograph/dataset.h"
#include "cartograph/render/viewport.h"

namespace cartograph::render {

class RenderError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Renderer {
public:
    // Renders every layer of the dataset into an off-screen bitmap using the
    // given viewport. Forces the D2D software (WARP) rasterizer so output
    // doesn't depend on the local GPU/driver - this is what makes
    // golden-image comparisons reproducible. Styling is a hardcoded
    // placeholder until Phase 7 introduces real symbology.
    static Microsoft::WRL::ComPtr<IWICBitmap> render(const Dataset& dataset, const Viewport& viewport);
};

// Encodes a 32bpp WIC bitmap to a PNG file. Throws RenderError on failure.
void savePng(IWICBitmap* bitmap, const std::string& path);

}  // namespace cartograph::render
