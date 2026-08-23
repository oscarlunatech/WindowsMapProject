#pragma once

#include <cstddef>
#include <vector>

#include "cartograph/index/spatial_index.h"
#include "cartograph/layer.h"

namespace cartograph::render {

// Everything needed to draw a layer fast, built once and reused every frame:
// a spatial index for viewport culling, plus geometry simplified at a small
// number of precomputed tolerance buckets so simplification cost isn't paid
// per frame, only once at construction ("Douglas-Peucker simplification,
// precomputed per zoom level, cached").
class LayerCache {
public:
    explicit LayerCache(const Layer& layer);

    // Indices into the original layer's features() that intersect viewExtent.
    std::vector<std::size_t> query(const Envelope& viewExtent) const;

    // Simplified geometry for featureIndex, from the precomputed bucket
    // closest to (without exceeding) the detail implied by mapUnitsPerPixel.
    const Geometry& simplifiedGeometry(std::size_t featureIndex, double mapUnitsPerPixel) const;

private:
    index::SpatialIndex spatialIndex_;
    std::vector<double> tolerances_;                    // ascending, tolerances_[0] == 0
    std::vector<std::vector<Geometry>> simplifiedByBucket_;  // [bucket][featureIndex]
};

}  // namespace cartograph::render
