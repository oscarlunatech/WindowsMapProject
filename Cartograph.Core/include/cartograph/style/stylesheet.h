#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cartograph/map.h"
#include "cartograph/style/style_spec.h"
#include "cartograph/style/symbol.h"

namespace cartograph::style {

// A StyleSpec bound to one concrete Map: layer names resolved to layer
// indices, field names resolved to field indices, and - the real point of the
// class - every feature's symbol resolved once, at construction, into a flat
// lookup table.
//
// Resolution is precomputed rather than evaluated per frame on purpose. A
// categorized renderer otherwise costs a string comparison per visible feature
// per frame, and a graduated one a break search, for an answer that can never
// change: features are immutable once Dataset::open() returns. This is the
// same bargain LayerCache already makes for simplification ("precompute a few
// buckets once, look one up every frame"), and it keeps drawDatasetCulled's
// hot loop down to an array index.
//
// Symbols are deduplicated by value, so a stylesheet that styles all 20 layers
// of a dataset identically has exactly one entry - which is what lets the
// renderer keep batching a whole layer into a single ID2D1PathGeometry in the
// common case (see drawDatasetCulled).
//
// Deliberately separate from LayerCache rather than folded into it: a
// LayerCache is style-independent (an R-tree and simplified geometry), so
// restyling must not invalidate it, and vice versa.
class Stylesheet {
public:
    // Every layer drawn with a default-constructed Symbol - i.e. exactly the
    // styling renderer.cpp hardcoded before Phase 7 existed.
    static Stylesheet defaults(const Map& map);

    // Throws StyleError if spec names a layer or a field this map doesn't
    // have (a typo in a hand-written style file is worth reporting, since
    // silently falling back would look exactly like the file being ignored),
    // or if a graduated renderer targets a non-numeric field.
    //
    // Styles are matched by layer *name*, so if two files in the map happen to
    // contain a layer of the same name they deliberately share a style - which
    // is what you want for e.g. 21 counties' worth of identically-shaped road
    // layers, and is also what the style file's "default" key is for.
    Stylesheet(const StyleSpec& spec, const Map& map);

    std::size_t symbolCount() const { return symbols_.size(); }
    const std::vector<Symbol>& symbols() const { return symbols_; }
    const Symbol& symbol(std::size_t symbolIndex) const { return symbols_[symbolIndex]; }

    // How many layers this stylesheet was built against - callers that hold a
    // Dataset and a Stylesheet separately can check they still belong together.
    std::size_t layerCount() const { return symbolIndexByFeature_.size(); }

    // Index into symbols() for one feature. layerIndex and featureIndex are
    // positions in Dataset::layers() and Layer::features() respectively.
    std::size_t symbolIndex(std::size_t layerIndex, std::size_t featureIndex) const {
        return symbolIndexByFeature_[layerIndex][featureIndex];
    }

private:
    std::vector<Symbol> symbols_;
    std::vector<std::vector<std::uint32_t>> symbolIndexByFeature_;  // [layer][feature] -> symbols_ index
};

}  // namespace cartograph::style
