#pragma once

#include <cstddef>
#include <vector>

#include "cartograph/dataset.h"
#include "cartograph/geometry.h"
#include "cartograph/render/layer_cache.h"

namespace cartograph::query {

struct Hit {
    std::size_t layerIndex;
    std::size_t featureIndex;
    double distance;  // map units; exactly 0 when the point falls inside a polygon
};

// Every feature within tolerance (map units) of mapPoint.
//
// Ordered the way an identify result should read: **topmost layer first**,
// then nearest first within a layer. Later layers draw over earlier ones (see
// drawDatasetCulled), so the highest layer index is what's actually visible
// under the cursor and belongs at the top of the list.
//
// layerCaches must be one per dataset.layers(), in order - the same vector the
// renderer already holds, which is the whole reason identify is cheap. Each
// cache's R-tree narrows candidates to features whose *extent* is near the
// point; every candidate then gets an exact geom::distanceTo test, since an
// overlapping bounding box says very little about the geometry inside it (a
// diagonal road's extent covers a lot of ground the road doesn't).
//
// A tolerance of 0 is meaningful for polygons - it asks "which polygons
// contain this point" - but will essentially never match a line or a point
// feature, since that requires landing on it exactly.
std::vector<Hit> identify(const Dataset& dataset, const std::vector<render::LayerCache>& layerCaches,
                           Point2D mapPoint, double tolerance);

}  // namespace cartograph::query
