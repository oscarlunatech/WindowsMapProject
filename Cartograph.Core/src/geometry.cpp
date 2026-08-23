#include "cartograph/geometry.h"

#include <algorithm>

namespace cartograph {

void Envelope::expand(const Point2D& point) {
    if (!valid) {
        minX = maxX = point.x;
        minY = maxY = point.y;
        valid = true;
        return;
    }
    minX = std::min(minX, point.x);
    maxX = std::max(maxX, point.x);
    minY = std::min(minY, point.y);
    maxY = std::max(maxY, point.y);
}

void Envelope::expand(const Envelope& other) {
    if (!other.valid) {
        return;
    }
    expand(Point2D{other.minX, other.minY});
    expand(Point2D{other.maxX, other.maxY});
}

Geometry::Geometry(GeometryType type, std::vector<Part> parts)
    : type_(type), parts_(std::move(parts)) {}

Envelope Geometry::extent() const {
    Envelope env;
    for (const Part& part : parts_) {
        for (const Ring& ring : part) {
            for (const Point2D& point : ring) {
                env.expand(point);
            }
        }
    }
    return env;
}

}  // namespace cartograph
