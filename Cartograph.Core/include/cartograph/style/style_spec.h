#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "cartograph/feature.h"
#include "cartograph/raster/raster_source.h"
#include "cartograph/style/symbol.h"

namespace cartograph::style {

class StyleError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Every feature in the layer draws with the same symbol.
struct SingleSymbol {
    Symbol symbol;
};

// Symbol chosen by matching one attribute field's value against a list of
// categories - the "unique values" renderer.
struct Categorized {
    struct Category {
        AttributeValue value;
        Symbol symbol;
    };

    std::string field;
    std::vector<Category> categories;  // tested in order, first match wins
    Symbol fallback;                   // nothing matched, or the value is null
};

// Symbol chosen by which numeric range the attribute falls into.
struct Graduated {
    struct Break {
        double upperBound;  // inclusive: a value <= upperBound selects this break
        Symbol symbol;
    };

    std::string field;
    std::vector<Break> breaks;  // ascending by upperBound
    Symbol fallback;            // above the last break, null, or non-numeric
};

// How a raster layer is drawn. Wraps raster::RasterStyle rather than
// redefining it, so the style file and the reader agree by construction.
struct RasterSymbol {
    raster::RasterStyle raster;
};

using LayerStyle = std::variant<SingleSymbol, Categorized, Graduated, RasterSymbol>;

// A whole style file, still dataset-independent: layers are named rather than
// indexed and field names haven't been resolved to field indices yet. Binding
// one to a concrete Dataset - and reporting a name that doesn't exist there -
// is Stylesheet's job.
struct StyleSpec {
    std::map<std::string, LayerStyle> byLayerName;
    std::optional<LayerStyle> defaultStyle;  // used for layers with no entry above
};

// Parses a JSON style file. Throws StyleError on unreadable/malformed input,
// an unknown renderer type, an unknown key, or a bad color literal. Layer and
// field *names* are not validated here - there's no dataset in scope yet, see
// Stylesheet's constructor for that.
StyleSpec loadStyleSpec(const std::string& path);

// Same, straight from an in-memory JSON string (what the unit tests use).
StyleSpec parseStyleSpec(const std::string& json);

}  // namespace cartograph::style
