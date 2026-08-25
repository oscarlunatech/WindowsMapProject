#pragma once

#include "cartograph/geometry.h"

namespace cartograph::geom {

// Shortest distance from point to the segment ab, in whatever units the
// coordinates are in. Zero if point lies on the segment. A degenerate
// segment (a == b) reduces to the distance to that point.
double distanceToSegment(Point2D point, Point2D a, Point2D b);

// Even-odd ray casting: true if point is inside ring. The ring is treated as
// implicitly closed, so it doesn't matter whether the source data repeats the
// first vertex at the end (OGR usually does, Core's own test fixtures often
// don't). Points exactly on the boundary are not guaranteed either way -
// that's inherent to ray casting, and why distanceTo() below pairs this with
// a boundary-distance test rather than relying on it alone.
bool pointInRing(Point2D point, const Ring& ring);

// Shortest distance from point to geometry, in map units:
//   Point / MultiPoint            distance to the nearest vertex
//   LineString / MultiLineString  distance to the nearest segment
//   Polygon / MultiPolygon        0 if inside (holes respected), otherwise
//                                 distance to the nearest boundary segment
//
// An empty or Unknown geometry returns infinity, so it can never register as
// a hit no matter how large the caller's tolerance is.
double distanceTo(const Geometry& geometry, Point2D point);

}  // namespace cartograph::geom
