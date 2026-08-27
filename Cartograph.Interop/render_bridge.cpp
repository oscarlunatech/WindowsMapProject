// Unmanaged. See render_bridge.h for why, and the CompileAsManaged setting on
// this file in Cartograph.Interop.vcxproj for how.

#include "render_bridge.h"

#include "cartograph/map.h"
#include "cartograph/render/renderer.h"
#include "cartograph/render/viewport.h"
#include "cartograph/style/stylesheet.h"

namespace cartograph_bridge {
namespace {

void copyPixels(IWICBitmap* bitmap, BgraPixels& out) {
    UINT width = 0;
    UINT height = 0;
    HRESULT hr = bitmap->GetSize(&width, &height);
    if (FAILED(hr)) {
        throw RenderFailure("IWICBitmap::GetSize failed (hr=" + std::to_string(hr) + ")");
    }

    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);

    const UINT stride = width * 4;
    out.bytes.assign(static_cast<std::size_t>(stride) * height, 0);
    if (out.bytes.empty()) {
        return;
    }

    hr = bitmap->CopyPixels(nullptr, stride, static_cast<UINT>(out.bytes.size()), out.bytes.data());
    if (FAILED(hr)) {
        throw RenderFailure("IWICBitmap::CopyPixels failed (hr=" + std::to_string(hr) + ")");
    }
}

}  // namespace

void refreshRasterLayers(cartograph::Map& map, const cartograph::render::Viewport& viewport,
                         const cartograph::style::Stylesheet& stylesheet) {
    try {
        cartograph::render::refreshRasterLayers(map, viewport, stylesheet);
    } catch (const cartograph::render::RenderError& e) {
        throw RenderFailure(e.what());
    }
}

void renderToPixels(const cartograph::Map& map, const cartograph::render::Viewport& viewport,
                    const cartograph::style::Stylesheet& stylesheet, BgraPixels& out) {
    try {
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap =
            cartograph::render::Renderer::render(map, viewport, stylesheet);
        copyPixels(bitmap.Get(), out);
    } catch (const cartograph::render::RenderError& e) {
        throw RenderFailure(e.what());
    }
}

void renderToPng(const cartograph::Map& map, const cartograph::render::Viewport& viewport,
                 const cartograph::style::Stylesheet& stylesheet, const std::string& path) {
    try {
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap =
            cartograph::render::Renderer::render(map, viewport, stylesheet);
        cartograph::render::savePng(bitmap.Get(), path);
    } catch (const cartograph::render::RenderError& e) {
        throw RenderFailure(e.what());
    }
}

}  // namespace cartograph_bridge
