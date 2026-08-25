#include <algorithm>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cartograph/style/style_spec.h"

namespace cartograph::style {

namespace {

using Json = nlohmann::json;

// Every symbol key the parser accepts, mapped onto Symbol's members. Unknown
// keys are rejected rather than ignored - a style file is hand-written, and a
// silently-dropped "linewidth" typo looks exactly like the renderer being
// broken.
constexpr const char* kSymbolKeys[] = {"fill",      "outline", "outlineWidth", "line",
                                        "lineWidth", "point",   "pointRadius"};

[[noreturn]] void fail(const std::string& message) { throw StyleError(message); }

int hexDigit(char c, const std::string& literal) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    fail(std::format("color '{}' contains a non-hexadecimal digit '{}'", literal, c));
}

// Accepts #rgb, #rrggbb and #rrggbbaa. Alpha defaults to fully opaque.
Color parseColor(const std::string& text) {
    if (text.empty() || text.front() != '#') {
        fail(std::format("color '{}' must start with '#' (e.g. \"#3366cc\")", text));
    }

    const std::string digits = text.substr(1);
    std::vector<int> channels;
    if (digits.size() == 3) {
        for (const char c : digits) {
            const int v = hexDigit(c, text);
            channels.push_back(v * 16 + v);  // #abc -> #aabbcc
        }
        channels.push_back(255);
    } else if (digits.size() == 6 || digits.size() == 8) {
        for (std::size_t i = 0; i < digits.size(); i += 2) {
            channels.push_back(hexDigit(digits[i], text) * 16 + hexDigit(digits[i + 1], text));
        }
        if (channels.size() == 3) {
            channels.push_back(255);
        }
    } else {
        fail(std::format("color '{}' must be #rgb, #rrggbb or #rrggbbaa", text));
    }

    return Color{static_cast<float>(channels[0]) / 255.0f, static_cast<float>(channels[1]) / 255.0f,
                 static_cast<float>(channels[2]) / 255.0f, static_cast<float>(channels[3]) / 255.0f};
}

Color colorAt(const Json& object, const char* key, Color fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object[key].is_string()) {
        fail(std::format("symbol key '{}' must be a color string like \"#3366cc\"", key));
    }
    return parseColor(object[key].get<std::string>());
}

float widthAt(const Json& object, const char* key, float fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object[key].is_number()) {
        fail(std::format("symbol key '{}' must be a number", key));
    }
    const auto value = object[key].get<double>();
    if (value < 0.0) {
        fail(std::format("symbol key '{}' must not be negative (use 0 to draw nothing)", key));
    }
    return static_cast<float>(value);
}

// Each symbol object starts from Symbol's own defaults and overrides only the
// keys it names, so a style file need only say what differs from the built-in
// styling.
Symbol parseSymbol(const Json& object, const std::string& context) {
    if (!object.is_object()) {
        fail(std::format("{}: a symbol must be a JSON object", context));
    }
    for (const auto& [key, value] : object.items()) {
        const bool known = std::any_of(std::begin(kSymbolKeys), std::end(kSymbolKeys),
                                        [&key](const char* known) { return key == known; });
        if (!known) {
            fail(std::format("{}: unknown symbol key '{}'", context, key));
        }
    }

    const Symbol defaults;
    Symbol symbol;
    symbol.fill = colorAt(object, "fill", defaults.fill);
    symbol.polygonStroke = colorAt(object, "outline", defaults.polygonStroke);
    symbol.polygonStrokeWidth = widthAt(object, "outlineWidth", defaults.polygonStrokeWidth);
    symbol.lineStroke = colorAt(object, "line", defaults.lineStroke);
    symbol.lineStrokeWidth = widthAt(object, "lineWidth", defaults.lineStrokeWidth);
    symbol.pointFill = colorAt(object, "point", defaults.pointFill);
    symbol.pointRadius = widthAt(object, "pointRadius", defaults.pointRadius);
    return symbol;
}

Symbol parseSymbolAt(const Json& parent, const char* key, const std::string& context) {
    if (!parent.contains(key)) {
        return Symbol{};
    }
    return parseSymbol(parent[key], std::format("{}.{}", context, key));
}

AttributeValue parseCategoryValue(const Json& value, const std::string& context) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return value.get<std::int64_t>();
    }
    if (value.is_number()) {
        return value.get<double>();
    }
    fail(std::format("{}: a category value must be a string or a number", context));
}

Categorized parseCategorized(const Json& object, const std::string& context) {
    Categorized categorized;
    if (!object.contains("field") || !object["field"].is_string()) {
        fail(std::format("{}: a categorized style needs a \"field\" string", context));
    }
    categorized.field = object["field"].get<std::string>();

    if (!object.contains("categories") || !object["categories"].is_array()) {
        fail(std::format("{}: a categorized style needs a \"categories\" array", context));
    }
    for (std::size_t i = 0; i < object["categories"].size(); ++i) {
        const Json& entry = object["categories"][i];
        const std::string entryContext = std::format("{}.categories[{}]", context, i);
        if (!entry.is_object() || !entry.contains("value")) {
            fail(std::format("{}: each category needs a \"value\" and a \"symbol\"", entryContext));
        }
        categorized.categories.push_back(
            Categorized::Category{parseCategoryValue(entry["value"], entryContext),
                                   parseSymbolAt(entry, "symbol", entryContext)});
    }

    categorized.fallback = parseSymbolAt(object, "fallback", context);
    return categorized;
}

Graduated parseGraduated(const Json& object, const std::string& context) {
    Graduated graduated;
    if (!object.contains("field") || !object["field"].is_string()) {
        fail(std::format("{}: a graduated style needs a \"field\" string", context));
    }
    graduated.field = object["field"].get<std::string>();

    if (!object.contains("breaks") || !object["breaks"].is_array()) {
        fail(std::format("{}: a graduated style needs a \"breaks\" array", context));
    }
    for (std::size_t i = 0; i < object["breaks"].size(); ++i) {
        const Json& entry = object["breaks"][i];
        const std::string entryContext = std::format("{}.breaks[{}]", context, i);
        if (!entry.is_object() || !entry.contains("max") || !entry["max"].is_number()) {
            fail(std::format("{}: each break needs a numeric \"max\" and a \"symbol\"", entryContext));
        }
        graduated.breaks.push_back(
            Graduated::Break{entry["max"].get<double>(), parseSymbolAt(entry, "symbol", entryContext)});
    }

    // Resolution walks breaks in order and takes the first whose max the value
    // doesn't exceed, so an out-of-order list would silently misclassify.
    for (std::size_t i = 1; i < graduated.breaks.size(); ++i) {
        if (graduated.breaks[i].upperBound < graduated.breaks[i - 1].upperBound) {
            fail(std::format("{}: \"breaks\" must be in ascending order by \"max\" ({} follows {})", context,
                              graduated.breaks[i].upperBound, graduated.breaks[i - 1].upperBound));
        }
    }

    graduated.fallback = parseSymbolAt(object, "fallback", context);
    return graduated;
}

LayerStyle parseLayerStyle(const Json& object, const std::string& context) {
    if (!object.is_object()) {
        fail(std::format("{}: a layer style must be a JSON object", context));
    }
    const std::string type = object.contains("type") && object["type"].is_string()
                                  ? object["type"].get<std::string>()
                                  : std::string("single");

    if (type == "single") {
        return SingleSymbol{parseSymbolAt(object, "symbol", context)};
    }
    if (type == "categorized") {
        return parseCategorized(object, context);
    }
    if (type == "graduated") {
        return parseGraduated(object, context);
    }
    fail(std::format("{}: unknown style type '{}' (expected \"single\", \"categorized\" or \"graduated\")",
                      context, type));
}

}  // namespace

StyleSpec parseStyleSpec(const std::string& json) {
    Json root;
    try {
        root = Json::parse(json);
    } catch (const Json::exception& e) {
        throw StyleError(std::string("style is not valid JSON: ") + e.what());
    }

    if (!root.is_object()) {
        fail("style must be a JSON object with a \"layers\" and/or \"default\" key");
    }
    for (const auto& [key, value] : root.items()) {
        if (key != "layers" && key != "default") {
            fail(std::format("unknown top-level style key '{}' (expected \"layers\" or \"default\")", key));
        }
    }

    StyleSpec spec;
    if (root.contains("default")) {
        spec.defaultStyle = parseLayerStyle(root["default"], "default");
    }
    if (root.contains("layers")) {
        if (!root["layers"].is_object()) {
            fail("\"layers\" must be a JSON object keyed by layer name");
        }
        for (const auto& [layerName, layerStyle] : root["layers"].items()) {
            spec.byLayerName.emplace(layerName, parseLayerStyle(layerStyle, std::format("layers.{}", layerName)));
        }
    }
    return spec;
}

StyleSpec loadStyleSpec(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw StyleError(std::format("failed to open style file '{}'", path));
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return parseStyleSpec(contents.str());
}

}  // namespace cartograph::style
