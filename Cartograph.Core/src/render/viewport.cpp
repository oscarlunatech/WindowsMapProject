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

// Grows `extent` about its own center along whichever axis is too narrow so
// its aspect ratio exactly matches the screen's, leaving scale uniform (no
// distortion) and no leftover space to letterbox/pillarbox: the map always
// fills the whole screen, just showing more map on the axis with slack.
Envelope expandToAspect(Envelope extent, ScreenSize screenSize) {
    const double mapWidth = extent.width();
    const double mapHeight = extent.height();
    if (mapWidth <= 0.0 || mapHeight <= 0.0 || screenSize.width <= 0 || screenSize.height <= 0) {
        return extent;
    }

    const double screenAspect = static_cast<double>(screenSize.width) / static_cast<double>(screenSize.height);
    const double mapAspect = mapWidth / mapHeight;
    const double centerX = (extent.minX + extent.maxX) / 2.0;
    const double centerY = (extent.minY + extent.maxY) / 2.0;

    if (mapAspect < screenAspect) {
        const double newWidth = mapHeight * screenAspect;
        extent.minX = centerX - newWidth / 2.0;
        extent.maxX = centerX + newWidth / 2.0;
    } else if (mapAspect > screenAspect) {
        const double newHeight = mapWidth / screenAspect;
        extent.minY = centerY - newHeight / 2.0;
        extent.maxY = centerY + newHeight / 2.0;
    }
    return extent;
}
}  // namespace

Viewport::Viewport(Envelope mapExtent, ScreenSize screenSize)
    : mapExtent_(expandToAspect(mapExtent, screenSize)), screenSize_(screenSize) {
    scale_ = safeScale(static_cast<double>(screenSize_.width), mapExtent_.width());
}

Point2D Viewport::mapToScreen(Point2D mapPoint) const {
    const double screenX = (mapPoint.x - mapExtent_.minX) * scale_;
    const double screenY = static_cast<double>(screenSize_.height) - (mapPoint.y - mapExtent_.minY) * scale_;
    return Point2D{screenX, screenY};
}

Point2D Viewport::screenToMap(Point2D screenPoint) const {
    const double mapX = mapExtent_.minX + screenPoint.x / scale_;
    const double mapY = mapExtent_.minY + (static_cast<double>(screenSize_.height) - screenPoint.y) / scale_;
    return Point2D{mapX, mapY};
}

}  // namespace cartograph::render
