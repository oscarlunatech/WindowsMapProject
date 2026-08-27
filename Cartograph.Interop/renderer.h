#pragma once

#include "map.h"
#include "stylesheet.h"
#include "viewport.h"

namespace Cartograph {
namespace Interop {

// A rendered frame as plain pixels.
//
// The format is premultiplied BGRA in top-down row order (row 0 is the top of
// the image), Stride bytes per row - which is not an arbitrary choice: Core's
// off-screen target
// is a WIC bitmap in GUID_WICPixelFormat32bppPBGRA, and that is byte-for-byte
// what WPF calls PixelFormats.Pbgra32. A shell can hand Pixels straight to
// WriteableBitmap::WritePixels or BitmapSource::Create with no conversion at
// all, which is the entire reason this type is shaped this way rather than as
// something friendlier-looking.
public ref class RenderedImage sealed {
public:
    property int Width {
        int get() { return width_; }
    }

    property int Height {
        int get() { return height_; }
    }

    // Bytes per row - always Width * 4 here, but spelled out because every
    // .NET bitmap API asks for it.
    property int Stride {
        int get() { return width_ * 4; }
    }

    property array<System::Byte> ^ Pixels {
        array<System::Byte> ^ get() { return pixels_; }
    }

internal:
    RenderedImage(int width, int height, array<System::Byte> ^ pixels)
        : width_(width), height_(height), pixels_(pixels) {}

private:
    int width_;
    int height_;
    array<System::Byte> ^ pixels_;
};

// cartograph::render's one-shot drawing entry points.
//
// **This is deliberately the slow path.** It mirrors Renderer::render, which
// calls drawMap - unculled, unbatched, single-threaded - and which Core keeps
// untouched by performance work on purpose so the golden-image test can never
// be perturbed by it (see the Phase 4 DECISIONS entry). It is the right thing
// for an export, a thumbnail, or a first paint.
//
// It is *not* how the shell should drive an interactive map. The fast path is
// drawMapCulled writing into a live ID2D1HwndRenderTarget, which needs a
// window to render into - so it belongs with the hosted map surface in Phase
// 13 rather than here.
public ref class Renderer abstract sealed {
public:
    // Re-reads every visible raster layer's window for this viewport and
    // stores the result on the layer, so a following Render has current
    // pixels. Synchronous and does file I/O: call it off the UI thread.
    // A map with no raster layers makes this a no-op.
    static void RefreshRasterLayers(Map ^ map, Viewport ^ viewport, Stylesheet ^ stylesheet);

    // Renders to an off-screen bitmap and returns its pixels.
    static RenderedImage ^ Render(Map ^ map, Viewport ^ viewport, Stylesheet ^ stylesheet);

    // Renders and encodes straight to a PNG file, without the pixels ever
    // crossing into managed memory.
    static void RenderToPng(Map ^ map, Viewport ^ viewport, Stylesheet ^ stylesheet,
                            System::String ^ path);
};

}  // namespace Interop
}  // namespace Cartograph
