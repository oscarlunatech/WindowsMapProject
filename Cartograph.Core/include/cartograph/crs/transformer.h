#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include "cartograph/geometry.h"

namespace cartograph::crs {

class CrsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Transforms points from one CRS to another via PROJ - never hand-rolled
// datum math, same rule the Phase 3 DECISIONS.md entry applies to
// simplification vs. GEOS. Hides proj.h behind a pimpl so it never appears
// in a public header, same pattern as index::SpatialIndex hiding
// boost::geometry::index::rtree.
class Transformer {
public:
    // sourceCrsWkt/targetCrs may be WKT or any other PROJ-recognized CRS
    // string (e.g. "EPSG:4326"). Throws CrsError if either can't be parsed
    // or no transform pipeline between them can be found.
    Transformer(const std::string& sourceCrsWkt, const std::string& targetCrs);
    ~Transformer();

    Transformer(const Transformer&) = delete;
    Transformer& operator=(const Transformer&) = delete;
    Transformer(Transformer&&) noexcept;
    Transformer& operator=(Transformer&&) noexcept;

    Point2D transform(Point2D point) const;

    // True if source and target are equivalent CRSs (per PROJ's own
    // comparison, not a string match) - transform() is then guaranteed to
    // return its input unchanged rather than merely numerically very close.
    bool isIdentity() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cartograph::crs
