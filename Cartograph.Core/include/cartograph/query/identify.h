#pragma once

#include <cstddef>
#include <vector>

#include "cartograph/geometry.h"
#include "cartograph/map.h"

namespace cartograph::query {

struct Hit {
    std::size_t layerIndex;  // index into map.layers()
    std::size_t featureIndex;
    double distance;  // map units; exactly 0 when the point falls inside a polygon
};

// Every feature within tolerance (map units) of mapPoint.
//
// Ordered the way an identify result should read: **topmost layer first**,
// then nearest first within a layer. Later layers draw over earlier ones, so
// the highest layer index is what's actually visible under the cursor and
// belongs at the top of the list.
//
// Hidden layers are skipped entirely - clicking something you can't see
// shouldn't report a hit.
//
// Each layer's LayerCache narrows candidates to features whose *extent* is
// near the point; every candidate then gets an exact geom::distanceTo test,
// since an overlapping bounding box says very little about the geometry inside
// it (a diagonal road's extent covers a lot of ground the road doesn't). The
// test runs against the layer's original geometry, not the cache's
// zoom-simplified copy, so identify answers about the data rather than about
// whatever the current zoom rounded it to.
//
// A tolerance of 0 is meaningful for polygons - it asks "which polygons
// contain this point" - but will essentially never match a line or a point
// feature, since that requires landing on it exactly.
std::vector<Hit> identify(const Map& map, Point2D mapPoint, double tolerance);

}  // namespace cartograph::query
