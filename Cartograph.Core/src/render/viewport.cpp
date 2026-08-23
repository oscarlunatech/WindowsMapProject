#include "cartograph/render/viewport.h"

#include <algorithm>

namespace cartograph::render {

namespace {
double safeScale(double screenExtent, double mapExtent) {
    if (mapExtent <= 0.0) {
        return 1.0;
    }
    return screenExtent / mapExtent;
}
}  // namespace

Viewport::Viewport(Envelope mapExtent, ScreenSize screenSize)
    : mapExtent_(mapExtent), screenSize_(screenSize) {
    const double scaleX = safeScale(static_cast<double>(screenSize_.width), mapExtent_.width());
    const double scaleY = safeScale(static_cast<double>(screenSize_.height), mapExtent_.height());
    scale_ = std::min(scaleX, scaleY);

    const double drawnWidth = mapExtent_.width() * scale_;
    const double drawnHeight = mapExtent_.height() * scale_;
    offsetX_ = (static_cast<double>(screenSize_.width) - drawnWidth) / 2.0;
    offsetY_ = (static_cast<double>(screenSize_.height) - drawnHeight) / 2.0;
}

Point2D Viewport::mapToScreen(Point2D mapPoint) const {
    const double screenX = offsetX_ + (mapPoint.x - mapExtent_.minX) * scale_;
    const double screenY =
        static_cast<double>(screenSize_.height) - offsetY_ - (mapPoint.y - mapExtent_.minY) * scale_;
    return Point2D{screenX, screenY};
}

Point2D Viewport::screenToMap(Point2D screenPoint) const {
    const double mapX = mapExtent_.minX + (screenPoint.x - offsetX_) / scale_;
    const double mapY =
        mapExtent_.minY + (static_cast<double>(screenSize_.height) - offsetY_ - screenPoint.y) / scale_;
    return Point2D{mapX, mapY};
}

}  // namespace cartograph::render
