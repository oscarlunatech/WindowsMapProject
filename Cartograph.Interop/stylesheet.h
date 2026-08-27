#pragma once

#include "cartograph/style/stylesheet.h"

#include "map.h"
#include "types.h"

namespace Cartograph {
namespace Interop {

// cartograph::style::Stylesheet: a style spec bound to one concrete Map, with
// every feature's symbol resolved once at construction.
//
// Two lifetime rules cross the boundary from Core and both matter to the
// shell:
//
//  * A Stylesheet belongs to the Map it was built against. Rendering with a
//    mismatched pair throws (Core checks the layer count), so keep them
//    together.
//  * Map::SetDisplayCrs replaces every layer, which invalidates the
//    stylesheet. Rebuild after reprojecting.
//
// Construction is O(features), so build one when the map loads and keep it -
// not per frame.
public ref class Stylesheet sealed {
public:
    // Every layer drawn with Core's built-in default symbol - the exact
    // styling that predates Phase 7, and what the golden-image test pins.
    static Stylesheet ^ Defaults(Map ^ map);

    // Reads a JSON style file (see README's Styling section) and binds it.
    // Throws StyleException for a malformed file, an unknown key, or a layer
    // or field name this map does not have - Core reports those rather than
    // falling back silently, and so does this.
    static Stylesheet ^ FromFile(System::String ^ path, Map ^ map);

    // The same, from an in-memory JSON string.
    static Stylesheet ^ FromJson(System::String ^ json, Map ^ map);

    ~Stylesheet() { this->!Stylesheet(); }
    !Stylesheet();

    // Distinct symbols after deduplication by value - not one per feature.
    property int SymbolCount {
        int get();
    }

    Symbol GetSymbol(int symbolIndex);

    // How many layers this was built against; compare with Map::LayerCount to
    // check the two still belong together.
    property int LayerCount {
        int get();
    }

    // Which entry in GetSymbol one feature resolves to. This is the lookup the
    // renderer does per feature per frame, exposed so a symbology editor can
    // show what a feature actually draws as.
    int SymbolIndex(int layerIndex, int featureIndex);

internal:
    cartograph::style::Stylesheet& Native();

private:
    Stylesheet(cartograph::style::Stylesheet* native) : native_(native) {}

    cartograph::style::Stylesheet* native_;
};

}  // namespace Interop
}  // namespace Cartograph
