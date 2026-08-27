#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace cartograph {
class Map;
namespace render {
class Viewport;
}
namespace style {
class Stylesheet;
}
}  // namespace cartograph

// A plain-C++ shim over cartograph::render, and the only part of this assembly
// compiled **unmanaged** (see CompileAsManaged in the .vcxproj).
//
// It exists because of a hard toolchain constraint, not a design preference:
// <wrl/client.h> refuses to compile under /clr at all - not a warning, a
// deliberate `#error WRL cannot be compiled with /clr option enabled` in the
// Windows SDK. Core's render/renderer.h includes it, because Renderer::render
// hands back a ComPtr<IWICBitmap>. So *no* managed translation unit in this
// assembly can include that header, and since errors.h needs render::RenderError
// the contamination would otherwise reach every file here.
//
// Confining it to one native translation unit is the smallest fix and the one
// that keeps the accommodation on the interop side. Core is not restructured to
// suit C++/CLI: renderer.h returning a ComPtr is the right shape for the native
// callers that outnumber this one, and it stays that way.
//
// The one thing that must cross back out is the error type, since managed code
// cannot name render::RenderError without including the poisoned header - hence
// RenderFailure below. Core's other exception types are all declared in
// wrl-free headers and pass straight through untranslated.
namespace cartograph_bridge {

// cartograph::render::RenderError, re-thrown as something managed code can
// name. Nothing else about it changes; the message is passed along verbatim.
class RenderFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Premultiplied BGRA, top-down, width * 4 bytes per row - i.e. exactly what
// Core's off-screen WIC target already holds, copied out without conversion.
struct BgraPixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bytes;
};

void refreshRasterLayers(cartograph::Map& map, const cartograph::render::Viewport& viewport,
                         const cartograph::style::Stylesheet& stylesheet);

void renderToPixels(const cartograph::Map& map, const cartograph::render::Viewport& viewport,
                    const cartograph::style::Stylesheet& stylesheet, BgraPixels& out);

void renderToPng(const cartograph::Map& map, const cartograph::render::Viewport& viewport,
                 const cartograph::style::Stylesheet& stylesheet, const std::string& path);

}  // namespace cartograph_bridge
