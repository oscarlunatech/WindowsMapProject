#pragma once

#include <vector>

namespace cartograph {

enum class GeometryType {
    Point,
    LineString,
    Polygon,
    MultiPoint,
    MultiLineString,
    MultiPolygon,
    Unknown
};

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

// A ring is one closed loop of points (a line, or one boundary of a polygon).
using Ring = std::vector<Point2D>;

// A part is one ring for Point/LineString; exterior ring followed by hole
// rings for Polygon. Multi* geometries have more than one part.
using Part = std::vector<Ring>;

struct Envelope {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    bool valid = false;

    void expand(const Point2D& point);
    void expand(const Envelope& other);

    double width() const { return maxX - minX; }
    double height() const { return maxY - minY; }
};

class Geometry {
public:
    Geometry() = default;
    Geometry(GeometryType type, std::vector<Part> parts);

    GeometryType type() const { return type_; }
    const std::vector<Part>& parts() const { return parts_; }
    Envelope extent() const;

private:
    GeometryType type_ = GeometryType::Unknown;
    std::vector<Part> parts_;
};

}  // namespace cartograph
