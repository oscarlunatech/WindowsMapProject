#include "cartograph/style/stylesheet.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <variant>

namespace cartograph::style {

namespace {

std::string joinFieldNames(const std::vector<FieldDef>& fields) {
    std::string joined;
    for (const FieldDef& field : fields) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += field.name;
    }
    return joined.empty() ? "(none)" : joined;
}

std::string joinLayerNames(const Dataset& dataset) {
    std::string joined;
    for (const Layer& layer : dataset.layers()) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += layer.name();
    }
    return joined.empty() ? "(none)" : joined;
}

// Symbols are stored once per distinct value, so the renderer's per-frame
// grouping (one batched geometry per symbol) stays as coarse as the style
// actually is - two layers styled identically share one batch key.
std::uint32_t internSymbol(std::vector<Symbol>& symbols, const Symbol& symbol) {
    const auto it = std::find(symbols.begin(), symbols.end(), symbol);
    if (it != symbols.end()) {
        return static_cast<std::uint32_t>(it - symbols.begin());
    }
    symbols.push_back(symbol);
    return static_cast<std::uint32_t>(symbols.size() - 1);
}

std::size_t findField(const Layer& layer, const std::string& fieldName) {
    const std::vector<FieldDef>& fields = layer.fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == fieldName) {
            return i;
        }
    }
    throw StyleError(std::format("layer '{}' has no field '{}' (available: {})", layer.name(), fieldName,
                                  joinFieldNames(fields)));
}

std::optional<double> asNumber(const AttributeValue& value) {
    if (const auto* i = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*i);
    }
    if (const auto* d = std::get_if<double>(&value)) {
        return *d;
    }
    return std::nullopt;
}

// A category value comes from JSON, where a number is just a number - but the
// same column surfaces from OGR as Integer, Integer64 or Real depending on the
// format. Compare numerically across those three rather than making the style
// file guess the column's exact OGR type. Strings compare exactly; a null
// attribute never matches a category, it takes the fallback.
bool attributeMatches(const AttributeValue& featureValue, const AttributeValue& categoryValue) {
    const std::optional<double> a = asNumber(featureValue);
    const std::optional<double> b = asNumber(categoryValue);
    if (a && b) {
        return *a == *b;
    }

    const auto* left = std::get_if<std::string>(&featureValue);
    const auto* right = std::get_if<std::string>(&categoryValue);
    return left != nullptr && right != nullptr && *left == *right;
}

// Attribute vectors are built in lockstep with fields() in dataset.cpp, but a
// short one would index out of bounds here rather than fail visibly, so treat
// a missing attribute as null (i.e. as the fallback).
const AttributeValue& attributeAt(const Feature& feature, std::size_t fieldIndex) {
    static const AttributeValue kNull{};
    const std::vector<AttributeValue>& attributes = feature.attributes();
    return fieldIndex < attributes.size() ? attributes[fieldIndex] : kNull;
}

std::vector<std::uint32_t> resolveSingle(std::vector<Symbol>& symbols, const Layer& layer,
                                          const SingleSymbol& style) {
    return std::vector<std::uint32_t>(layer.features().size(), internSymbol(symbols, style.symbol));
}

std::vector<std::uint32_t> resolveCategorized(std::vector<Symbol>& symbols, const Layer& layer,
                                               const Categorized& style) {
    const std::size_t fieldIndex = findField(layer, style.field);

    std::vector<std::uint32_t> categorySymbols;
    categorySymbols.reserve(style.categories.size());
    for (const Categorized::Category& category : style.categories) {
        categorySymbols.push_back(internSymbol(symbols, category.symbol));
    }
    const std::uint32_t fallback = internSymbol(symbols, style.fallback);

    const std::vector<Feature>& features = layer.features();
    std::vector<std::uint32_t> indices(features.size(), fallback);
    for (std::size_t f = 0; f < features.size(); ++f) {
        const AttributeValue& value = attributeAt(features[f], fieldIndex);
        for (std::size_t c = 0; c < style.categories.size(); ++c) {
            if (attributeMatches(value, style.categories[c].value)) {
                indices[f] = categorySymbols[c];
                break;
            }
        }
    }
    return indices;
}

std::vector<std::uint32_t> resolveGraduated(std::vector<Symbol>& symbols, const Layer& layer,
                                             const Graduated& style) {
    const std::size_t fieldIndex = findField(layer, style.field);
    if (layer.fields()[fieldIndex].type == FieldType::String) {
        throw StyleError(std::format(
            "graduated style on layer '{}' targets field '{}', which is a string field - graduated "
            "classification needs a numeric field",
            layer.name(), style.field));
    }

    std::vector<std::uint32_t> breakSymbols;
    breakSymbols.reserve(style.breaks.size());
    for (const Graduated::Break& brk : style.breaks) {
        breakSymbols.push_back(internSymbol(symbols, brk.symbol));
    }
    const std::uint32_t fallback = internSymbol(symbols, style.fallback);

    const std::vector<Feature>& features = layer.features();
    std::vector<std::uint32_t> indices(features.size(), fallback);
    for (std::size_t f = 0; f < features.size(); ++f) {
        const std::optional<double> value = asNumber(attributeAt(features[f], fieldIndex));
        if (!value) {
            continue;  // null or non-numeric: keep the fallback
        }
        for (std::size_t b = 0; b < style.breaks.size(); ++b) {
            if (*value <= style.breaks[b].upperBound) {
                indices[f] = breakSymbols[b];
                break;
            }
        }
    }
    return indices;
}

std::vector<std::uint32_t> resolveLayer(std::vector<Symbol>& symbols, const Layer& layer,
                                         const LayerStyle& style) {
    if (const auto* single = std::get_if<SingleSymbol>(&style)) {
        return resolveSingle(symbols, layer, *single);
    }
    if (const auto* categorized = std::get_if<Categorized>(&style)) {
        return resolveCategorized(symbols, layer, *categorized);
    }
    return resolveGraduated(symbols, layer, std::get<Graduated>(style));
}

}  // namespace

Stylesheet Stylesheet::defaults(const Dataset& dataset) {
    StyleSpec spec;
    spec.defaultStyle = SingleSymbol{};
    return Stylesheet(spec, dataset);
}

Stylesheet::Stylesheet(const StyleSpec& spec, const Dataset& dataset) {
    // Report a style file naming a layer this dataset doesn't have. It's
    // almost always a typo, and quietly drawing the default instead is
    // indistinguishable from the style file not being read at all.
    for (const auto& [layerName, layerStyle] : spec.byLayerName) {
        const bool exists = std::any_of(dataset.layers().begin(), dataset.layers().end(),
                                         [&layerName](const Layer& l) { return l.name() == layerName; });
        if (!exists) {
            throw StyleError(std::format("style names layer '{}', which this dataset doesn't have (available: {})",
                                          layerName, joinLayerNames(dataset)));
        }
    }

    const LayerStyle fallbackStyle = spec.defaultStyle.value_or(LayerStyle{SingleSymbol{}});

    symbolIndexByFeature_.reserve(dataset.layers().size());
    for (const Layer& layer : dataset.layers()) {
        const auto it = spec.byLayerName.find(layer.name());
        const LayerStyle& style = it != spec.byLayerName.end() ? it->second : fallbackStyle;
        symbolIndexByFeature_.push_back(resolveLayer(symbols_, layer, style));
    }

    // A dataset with no layers would otherwise leave an empty symbol table;
    // keep at least one so symbol(0) is always a valid call.
    if (symbols_.empty()) {
        symbols_.push_back(Symbol{});
    }
}

}  // namespace cartograph::style
