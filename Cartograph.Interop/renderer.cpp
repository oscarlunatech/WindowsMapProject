#include "renderer.h"

#include <cstring>
#include <string>

#include "conversions.h"
#include "errors.h"
#include "render_bridge.h"

using namespace System;

namespace Cartograph {
namespace Interop {
namespace {

// All three entry points take the same trio and would otherwise let a null
// reach a Core function that dereferences it.
void requireAll(Map ^ map, Viewport ^ viewport, Stylesheet ^ stylesheet) {
    if (map == nullptr) {
        throw gcnew ArgumentNullException("map");
    }
    if (viewport == nullptr) {
        throw gcnew ArgumentNullException("viewport");
    }
    if (stylesheet == nullptr) {
        throw gcnew ArgumentNullException("stylesheet");
    }
}

}  // namespace

void Renderer::RefreshRasterLayers(Map ^ map, Viewport ^ viewport, Stylesheet ^ stylesheet) {
    requireAll(map, viewport, stylesheet);
    cartograph::Map& nativeMap = map->Native();
    cartograph::render::Viewport& nativeViewport = viewport->Native();
    cartograph::style::Stylesheet& nativeStylesheet = stylesheet->Native();

    CARTOGRAPH_TRY
        cartograph_bridge::refreshRasterLayers(nativeMap, nativeViewport, nativeStylesheet);
    CARTOGRAPH_CATCH
}

RenderedImage ^ Renderer::Render(Map ^ map, Viewport ^ viewport, Stylesheet ^ stylesheet) {
    requireAll(map, viewport, stylesheet);
    cartograph::Map& nativeMap = map->Native();
    cartograph::render::Viewport& nativeViewport = viewport->Native();
    cartograph::style::Stylesheet& nativeStylesheet = stylesheet->Native();

    // Native buffer first, one managed copy after: the alternative - pinning a
    // managed array and rendering into it - would hold a GC pin for the whole
    // draw, which is exactly the kind of thing that fragments the heap of a
    // long-running shell repainting at speed.
    cartograph_bridge::BgraPixels pixels;
    CARTOGRAPH_TRY
        cartograph_bridge::renderToPixels(nativeMap, nativeViewport, nativeStylesheet, pixels);
    CARTOGRAPH_CATCH

    array<Byte> ^ managed = gcnew array<Byte>(static_cast<int>(pixels.bytes.size()));
    if (!pixels.bytes.empty()) {
        pin_ptr<Byte> pinned = &managed[0];
        std::memcpy(pinned, pixels.bytes.data(), pixels.bytes.size());
    }
    return gcnew RenderedImage(pixels.width, pixels.height, managed);
}

void Renderer::RenderToPng(Map ^ map, Viewport ^ viewport, Stylesheet ^ stylesheet, String ^ path) {
    requireAll(map, viewport, stylesheet);
    const std::string nativePath = Detail::ToNative(path);
    cartograph::Map& nativeMap = map->Native();
    cartograph::render::Viewport& nativeViewport = viewport->Native();
    cartograph::style::Stylesheet& nativeStylesheet = stylesheet->Native();

    CARTOGRAPH_TRY
        cartograph_bridge::renderToPng(nativeMap, nativeViewport, nativeStylesheet, nativePath);
    CARTOGRAPH_CATCH
}

}  // namespace Interop
}  // namespace Cartograph
