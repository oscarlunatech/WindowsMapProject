#pragma once

#include "cartograph/geometry.h"

namespace cartograph::geom {

// Ramer-Douglas-Peucker simplification: drops points whose perpendicular
// distance from the simplified line is within tolerance. First/last points
// are always kept. tolerance <= 0 (or fewer than 3 points) returns the ring
// unchanged.
Ring simplify(const Ring& ring, double tolerance);

// Applies simplify() to every ring of every part.
Geometry simplify(const Geometry& geometry, double tolerance);

}  // namespace cartograph::geom
