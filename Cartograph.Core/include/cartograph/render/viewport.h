#pragma once

#include "cartograph/geometry.h"

namespace cartograph::render {

struct ScreenSize {
    int width = 0;
    int height = 0;
};

// Maps a map-space extent onto a screen-space pixel rectangle and flips Y
// (screen Y grows downward; map Y grows north). Scale is uniform (no
// distortion), and the map always fills the entire screen: rather than
// letterboxing/pillarboxing when mapExtent's aspect ratio doesn't match
// screenSize's, the constructor grows mapExtent about its own center along
// whichever axis is too narrow, so mapExtent() may return a larger extent
// than the one passed in.
class Viewport {
public:
    Viewport(Envelope mapExtent, ScreenSize screenSize);

    Point2D mapToScreen(Point2D mapPoint) const;
    Point2D screenToMap(Point2D screenPoint) const;

    // The extent actually displayed - may be wider or taller than the
    // extent passed to the constructor; see class comment.
    const Envelope& mapExtent() const { return mapExtent_; }
    ScreenSize screenSize() const { return screenSize_; }

    // Screen pixels per map unit (uniform - no distortion).
    double scale() const { return scale_; }

private:
    Envelope mapExtent_;
    ScreenSize screenSize_;
    double scale_;
};

}  // namespace cartograph::render
