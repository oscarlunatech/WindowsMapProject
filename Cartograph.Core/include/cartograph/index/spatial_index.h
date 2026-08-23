#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "cartograph/layer.h"

namespace cartograph::index {

// Bulk-loaded R-tree over a layer's feature extents, for viewport culling
// ("bulk-load R-tree per layer, query by viewport rect"). Built once per
// layer and reused across many queries. boost::geometry::index::rtree
// stays entirely inside spatial_index.cpp - this header doesn't expose it.
class SpatialIndex {
public:
    explicit SpatialIndex(const Layer& layer);
    ~SpatialIndex();
    SpatialIndex(SpatialIndex&&) noexcept;
    SpatialIndex& operator=(SpatialIndex&&) noexcept;

    // Indices into layer.features() whose extent intersects queryExtent.
    std::vector<std::size_t> query(const Envelope& queryExtent) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cartograph::index
