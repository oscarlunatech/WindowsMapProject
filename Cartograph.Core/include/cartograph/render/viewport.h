#pragma once

#include "cartograph/geometry.h"

namespace cartograph::render {

struct ScreenSize {
    int width = 0;
    int height = 0;
};

// Maps a map-space extent onto a screen-space pixel rectangle, preserving
// aspect ratio (uniform scale, centered) and flipping Y (screen Y grows
// downward; map Y grows north).
class Viewport {
public:
    Viewport(Envelope mapExtent, ScreenSize screenSize);

    Point2D mapToScreen(Point2D mapPoint) const;
    Point2D screenToMap(Point2D screenPoint) const;

    const Envelope& mapExtent() const { return mapExtent_; }
    ScreenSize screenSize() const { return screenSize_; }

    // Screen pixels per map unit (uniform - aspect ratio is preserved).
    double scale() const { return scale_; }

private:
    Envelope mapExtent_;
    ScreenSize screenSize_;
    double scale_;
    double offsetX_;
    double offsetY_;
};

}  // namespace cartograph::render
