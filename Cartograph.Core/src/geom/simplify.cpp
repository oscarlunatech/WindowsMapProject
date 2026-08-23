#include "cartograph/geom/simplify.h"

#include <cmath>

namespace cartograph::geom {

namespace {

double perpendicularDistance(const Point2D& p, const Point2D& lineStart, const Point2D& lineEnd) {
    const double dx = lineEnd.x - lineStart.x;
    const double dy = lineEnd.y - lineStart.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared == 0.0) {
        const double ddx = p.x - lineStart.x;
        const double ddy = p.y - lineStart.y;
        return std::sqrt(ddx * ddx + ddy * ddy);
    }
    const double numerator =
        std::abs(dy * p.x - dx * p.y + lineEnd.x * lineStart.y - lineEnd.y * lineStart.x);
    return numerator / std::sqrt(lengthSquared);
}

void simplifyRecursive(const Ring& points, std::size_t first, std::size_t last, double tolerance,
                        std::vector<bool>& keep) {
    if (last <= first + 1) {
        return;
    }

    double maxDist = 0.0;
    std::size_t maxIndex = first;
    for (std::size_t i = first + 1; i < last; ++i) {
        const double dist = perpendicularDistance(points[i], points[first], points[last]);
        if (dist > maxDist) {
            maxDist = dist;
            maxIndex = i;
        }
    }

    if (maxDist > tolerance) {
        keep[maxIndex] = true;
        simplifyRecursive(points, first, maxIndex, tolerance, keep);
        simplifyRecursive(points, maxIndex, last, tolerance, keep);
    }
}

}  // namespace

Ring simplify(const Ring& ring, double tolerance) {
    if (ring.size() < 3 || tolerance <= 0.0) {
        return ring;
    }

    std::vector<bool> keep(ring.size(), false);
    keep.front() = true;
    keep.back() = true;
    simplifyRecursive(ring, 0, ring.size() - 1, tolerance, keep);

    Ring result;
    result.reserve(ring.size());
    for (std::size_t i = 0; i < ring.size(); ++i) {
        if (keep[i]) {
            result.push_back(ring[i]);
        }
    }
    return result;
}

Geometry simplify(const Geometry& geometry, double tolerance) {
    std::vector<Part> parts;
    parts.reserve(geometry.parts().size());
    for (const Part& part : geometry.parts()) {
        Part simplifiedPart;
        simplifiedPart.reserve(part.size());
        for (const Ring& ring : part) {
            simplifiedPart.push_back(simplify(ring, tolerance));
        }
        parts.push_back(std::move(simplifiedPart));
    }
    return Geometry(geometry.type(), std::move(parts));
}

}  // namespace cartograph::geom
