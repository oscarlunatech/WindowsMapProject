#include "cartograph/index/spatial_index.h"

#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>

namespace cartograph::index {

namespace {
namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

using BoostPoint = bg::model::point<double, 2, bg::cs::cartesian>;
using BoostBox = bg::model::box<BoostPoint>;
using RTreeValue = std::pair<BoostBox, std::size_t>;
using RTree = bgi::rtree<RTreeValue, bgi::quadratic<16>>;

BoostBox toBoostBox(const Envelope& e) {
    return BoostBox(BoostPoint(e.minX, e.minY), BoostPoint(e.maxX, e.maxY));
}
}  // namespace

struct SpatialIndex::Impl {
    RTree tree;
};

SpatialIndex::SpatialIndex(const Layer& layer) : impl_(std::make_unique<Impl>()) {
    std::vector<RTreeValue> entries;
    entries.reserve(layer.features().size());
    for (std::size_t i = 0; i < layer.features().size(); ++i) {
        const Envelope extent = layer.features()[i].geometry().extent();
        if (!extent.valid) {
            continue;
        }
        entries.emplace_back(toBoostBox(extent), i);
    }
    // Constructing from a range triggers bulk loading (packing algorithm),
    // much faster than inserting features one at a time.
    impl_->tree = RTree(entries);
}

SpatialIndex::~SpatialIndex() = default;
SpatialIndex::SpatialIndex(SpatialIndex&&) noexcept = default;
SpatialIndex& SpatialIndex::operator=(SpatialIndex&&) noexcept = default;

std::vector<std::size_t> SpatialIndex::query(const Envelope& queryExtent) const {
    std::vector<RTreeValue> results;
    impl_->tree.query(bgi::intersects(toBoostBox(queryExtent)), std::back_inserter(results));

    std::vector<std::size_t> indices;
    indices.reserve(results.size());
    for (const RTreeValue& r : results) {
        indices.push_back(r.second);
    }
    return indices;
}

}  // namespace cartograph::index
