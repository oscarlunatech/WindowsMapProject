#include "cartograph/geom/predicates.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace cartograph::geom {

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

double distance(Point2D a, Point2D b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Shortest distance from point to the polyline formed by ring's consecutive
// vertices. closed=true also considers the wrap-around segment from the last
// vertex back to the first, which is what makes a polygon boundary complete
// even when the source data doesn't repeat the first vertex.
double distanceToRing(Point2D point, const Ring& ring, bool closed) {
    if (ring.empty()) {
        return kInfinity;
    }
    if (ring.size() == 1) {
        return distance(point, ring[0]);
    }

    double best = kInfinity;
    for (std::size_t i = 0; i + 1 < ring.size(); ++i) {
        const double d = distanceToSegment(point, ring[i], ring[i + 1]);
        if (d < best) {
            best = d;
        }
    }
    if (closed && ring.size() > 2) {
        const double d = distanceToSegment(point, ring.back(), ring.front());
        if (d < best) {
            best = d;
        }
    }
    return best;
}

// A part is one polygon: parts[0] is the exterior ring, the rest are holes.
bool pointInPolygonPart(Point2D point, const Part& part) {
    if (part.empty() || !pointInRing(point, part.front())) {
        return false;
    }
    for (std::size_t i = 1; i < part.size(); ++i) {
        if (pointInRing(point, part[i])) {
            return false;  // inside a hole is outside the polygon
        }
    }
    return true;
}

}  // namespace

double distanceToSegment(Point2D point, Point2D a, Point2D b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;

    if (lengthSquared == 0.0) {
        return distance(point, a);
    }

    // Projection of point onto the infinite line, clamped to the segment.
    double t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);

    return distance(point, Point2D{a.x + t * dx, a.y + t * dy});
}

bool pointInRing(Point2D point, const Ring& ring) {
    if (ring.size() < 3) {
        return false;
    }

    bool inside = false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const Point2D& current = ring[i];
        const Point2D& previous = ring[j];

        // Does the horizontal ray at point.y cross this edge, and if so, is
        // the crossing to the right of point.x? Each crossing flips parity.
        const bool straddles = (current.y > point.y) != (previous.y > point.y);
        if (!straddles) {
            continue;
        }
        const double crossingX =
            current.x + (point.y - current.y) / (previous.y - current.y) * (previous.x - current.x);
        if (point.x < crossingX) {
            inside = !inside;
        }
    }
    return inside;
}

double distanceTo(const Geometry& geometry, Point2D point) {
    const std::vector<Part>& parts = geometry.parts();

    switch (geometry.type()) {
        case GeometryType::Point:
        case GeometryType::MultiPoint: {
            double best = kInfinity;
            for (const Part& part : parts) {
                for (const Ring& ring : part) {
                    for (const Point2D& vertex : ring) {
                        const double d = distance(point, vertex);
                        if (d < best) {
                            best = d;
                        }
                    }
                }
            }
            return best;
        }

        case GeometryType::LineString:
        case GeometryType::MultiLineString: {
            double best = kInfinity;
            for (const Part& part : parts) {
                for (const Ring& ring : part) {
                    const double d = distanceToRing(point, ring, /*closed=*/false);
                    if (d < best) {
                        best = d;
                    }
                }
            }
            return best;
        }

        case GeometryType::Polygon:
        case GeometryType::MultiPolygon: {
            double best = kInfinity;
            for (const Part& part : parts) {
                if (pointInPolygonPart(point, part)) {
                    return 0.0;
                }
                // Outside this part: still measure to its boundary, including
                // hole boundaries - a click just inside a hole is nearest to
                // the hole's edge, not the polygon's outer edge.
                for (const Ring& ring : part) {
                    const double d = distanceToRing(point, ring, /*closed=*/true);
                    if (d < best) {
                        best = d;
                    }
                }
            }
            return best;
        }

        case GeometryType::Unknown:
            break;
    }
    return kInfinity;
}

}  // namespace cartograph::geom
