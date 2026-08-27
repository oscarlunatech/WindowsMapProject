#pragma once

#include "cartograph/render/viewport.h"

#include "types.h"

namespace Cartograph {
namespace Interop {

// cartograph::render::Viewport - the map<->screen transform.
//
// Immutable, exactly as Core's is: there is no way to move or resize one, only
// to construct a new one. The shell keeps its own mutable extent and rebuilds a
// Viewport whenever the extent or the window size changes, which is what Viewer
// already does every frame.
//
// Note that MapExtent is the extent actually *displayed*, which may be wider or
// taller than the one passed in - the constructor grows the extent about its
// centre along whichever axis is too narrow so the map fills the window rather
// than being letterboxed.
public ref class Viewport sealed {
public:
    Viewport(Envelope mapExtent, ScreenSize screenSize);

    ~Viewport() { this->!Viewport(); }
    !Viewport();

    MapPoint MapToScreen(MapPoint mapPoint);
    MapPoint ScreenToMap(MapPoint screenPoint);

    property Envelope MapExtent {
        Envelope get();
    }

    property ScreenSize Size {
        ScreenSize get();
    }

    // Screen pixels per map unit (uniform - no distortion). The shell divides
    // a pixel tolerance by this to get the map-unit tolerance identify wants.
    property double Scale {
        double get();
    }

internal:
    cartograph::render::Viewport& Native();

private:
    cartograph::render::Viewport* native_;
};

}  // namespace Interop
}  // namespace Cartograph
