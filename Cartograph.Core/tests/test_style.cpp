#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <string>
#include <variant>

#include "cartograph/map.h"
#include "cartograph/style/style_spec.h"
#include "cartograph/style/stylesheet.h"

using namespace cartograph;
using namespace cartograph::style;

namespace {

std::string fixturePath(const std::string& name) {
    return std::string(CARTOGRAPH_TEST_FIXTURES_DIR) + "/" + name;
}

const Map& countries() {
    // Dataset's constructor is private (only Dataset::open builds one), so
    // these tests bind against the real fixture rather than a synthetic map.
    // Opened once - it's the same read-only data for every case.
    static const Map map = Map::open(fixturePath("ne_110m_admin_0_countries.shp"));
    return map;
}

// Index of the first feature whose CONTINENT attribute equals name.
std::size_t firstFeatureInContinent(const std::string& name) {
    const Layer& layer = countries().layers().front().layer();
    std::size_t continentField = 0;
    for (std::size_t i = 0; i < layer.fields().size(); ++i) {
        if (layer.fields()[i].name == "CONTINENT") {
            continentField = i;
        }
    }
    for (std::size_t f = 0; f < layer.features().size(); ++f) {
        const AttributeValue& value = layer.features()[f].attributes()[continentField];
        if (const auto* text = std::get_if<std::string>(&value); text != nullptr && *text == name) {
            return f;
        }
    }
    FAIL("fixture has no feature on continent " + name);
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

TEST_CASE("Default symbol reproduces the pre-Phase-7 hardcoded styling", "[style]") {
    // These four colors and three widths are what renderer.cpp hardcoded
    // before symbology existed. The golden-image test depends on them being
    // unchanged, so pin them here where a change is obvious rather than
    // leaving the golden PNG as the only thing that notices.
    const Symbol symbol;
    REQUIRE(symbol.fill == Color{0.85f, 0.85f, 0.85f, 1.0f});
    REQUIRE(symbol.polygonStroke == Color{0.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(symbol.polygonStrokeWidth == 1.0f);
    REQUIRE(symbol.lineStroke == Color{0.0f, 0.2f, 0.8f, 1.0f});
    REQUIRE(symbol.lineStrokeWidth == 1.5f);
    REQUIRE(symbol.pointFill == Color{1.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(symbol.pointRadius == 3.0f);
}

TEST_CASE("Default stylesheet resolves every feature to one shared symbol", "[style]") {
    const Stylesheet sheet = Stylesheet::defaults(countries());

    // One symbol for the whole map is what lets drawMapCulled keep
    // batching a layer into a single geometry, exactly as it did pre-Phase-7.
    REQUIRE(sheet.symbolCount() == 1);
    REQUIRE(sheet.symbol(0) == Symbol{});
    REQUIRE(sheet.layerCount() == countries().layers().size());

    for (std::size_t f = 0; f < countries().layers().front().layer().features().size(); ++f) {
        REQUIRE(sheet.symbolIndex(0, f) == 0);
    }
}

// ---------------------------------------------------------------------------
// Categorized
// ---------------------------------------------------------------------------

TEST_CASE("Categorized style assigns per-category symbols and a fallback", "[style]") {
    Categorized categorized;
    categorized.field = "CONTINENT";
    Symbol africa;
    africa.fill = Color{1.0f, 0.0f, 0.0f, 1.0f};
    Symbol europe;
    europe.fill = Color{0.0f, 1.0f, 0.0f, 1.0f};
    Symbol fallback;
    fallback.fill = Color{0.5f, 0.5f, 0.5f, 1.0f};
    categorized.categories = {{std::string("Africa"), africa}, {std::string("Europe"), europe}};
    categorized.fallback = fallback;

    StyleSpec spec;
    spec.byLayerName.emplace("ne_110m_admin_0_countries", categorized);
    const Stylesheet sheet(spec, countries());

    REQUIRE(sheet.symbolCount() == 3);  // two categories plus the fallback
    REQUIRE(sheet.symbol(sheet.symbolIndex(0, firstFeatureInContinent("Africa"))) == africa);
    REQUIRE(sheet.symbol(sheet.symbolIndex(0, firstFeatureInContinent("Europe"))) == europe);
    // Oceania isn't a listed category, so it takes the fallback.
    REQUIRE(sheet.symbol(sheet.symbolIndex(0, firstFeatureInContinent("Oceania"))) == fallback);
}

TEST_CASE("Categorized style matches numbers across OGR's integer and real types", "[style]") {
    // A style file just says 1; the column may arrive as Integer, Integer64 or
    // Real depending on the driver, so matching compares numerically rather
    // than requiring the file to name the exact variant alternative.
    Categorized categorized;
    categorized.field = "scalerank";
    Symbol matched;
    matched.fill = Color{0.1f, 0.2f, 0.3f, 1.0f};
    categorized.categories = {{static_cast<std::int64_t>(1), matched}};

    StyleSpec spec;
    spec.byLayerName.emplace("ne_110m_admin_0_countries", categorized);
    const Stylesheet sheet(spec, countries());

    bool anyMatched = false;
    for (std::size_t f = 0; f < countries().layers().front().layer().features().size(); ++f) {
        if (sheet.symbol(sheet.symbolIndex(0, f)) == matched) {
            anyMatched = true;
            break;
        }
    }
    REQUIRE(anyMatched);
}

TEST_CASE("Categorized style rejects an unknown field", "[style]") {
    Categorized categorized;
    categorized.field = "NOT_A_FIELD";

    StyleSpec spec;
    spec.byLayerName.emplace("ne_110m_admin_0_countries", categorized);
    REQUIRE_THROWS_AS(Stylesheet(spec, countries()), StyleError);
}

TEST_CASE("Stylesheet rejects a layer name the map doesn't have", "[style]") {
    StyleSpec spec;
    spec.byLayerName.emplace("roads", SingleSymbol{});
    REQUIRE_THROWS_AS(Stylesheet(spec, countries()), StyleError);
}

// ---------------------------------------------------------------------------
// Graduated
// ---------------------------------------------------------------------------

TEST_CASE("Graduated style classifies by break, inclusive of each upper bound", "[style]") {
    Graduated graduated;
    graduated.field = "POP_EST";
    Symbol small;
    small.fill = Color{0.9f, 0.9f, 1.0f, 1.0f};
    Symbol medium;
    medium.fill = Color{0.4f, 0.6f, 0.9f, 1.0f};
    Symbol large;
    large.fill = Color{0.0f, 0.2f, 0.5f, 1.0f};
    graduated.breaks = {{1'000'000.0, small}, {50'000'000.0, medium}};
    graduated.fallback = large;

    StyleSpec spec;
    spec.byLayerName.emplace("ne_110m_admin_0_countries", graduated);
    const Stylesheet sheet(spec, countries());

    // Fiji (~889,953) is under the first break; Tanzania (~58,005,463) is above
    // the last one and so takes the fallback. Both are the fixture's first two
    // features, in file order.
    REQUIRE(sheet.symbol(sheet.symbolIndex(0, 0)) == small);
    REQUIRE(sheet.symbol(sheet.symbolIndex(0, 1)) == large);
}

TEST_CASE("Graduated style rejects a string field", "[style]") {
    Graduated graduated;
    graduated.field = "NAME";
    graduated.breaks = {{1.0, Symbol{}}};

    StyleSpec spec;
    spec.byLayerName.emplace("ne_110m_admin_0_countries", graduated);
    REQUIRE_THROWS_AS(Stylesheet(spec, countries()), StyleError);
}

// ---------------------------------------------------------------------------
// Symbol interning
// ---------------------------------------------------------------------------

TEST_CASE("Identical symbols are deduplicated into one table entry", "[style]") {
    // Two categories that happen to look the same must not produce two batch
    // keys, or drawMapCulled would split one draw call into two for no
    // visible reason.
    Categorized categorized;
    categorized.field = "CONTINENT";
    Symbol same;
    same.fill = Color{0.25f, 0.5f, 0.75f, 1.0f};
    categorized.categories = {{std::string("Africa"), same}, {std::string("Europe"), same}};
    categorized.fallback = same;

    StyleSpec spec;
    spec.byLayerName.emplace("ne_110m_admin_0_countries", categorized);
    const Stylesheet sheet(spec, countries());

    REQUIRE(sheet.symbolCount() == 1);
}

// ---------------------------------------------------------------------------
// JSON parsing
// ---------------------------------------------------------------------------

TEST_CASE("Style JSON parses colors in #rgb, #rrggbb and #rrggbbaa", "[style]") {
    const StyleSpec spec = parseStyleSpec(R"({
        "default": { "symbol": { "fill": "#f00", "outline": "#0000ff", "line": "#00ff0080" } }
    })");

    REQUIRE(spec.defaultStyle.has_value());
    const Symbol& symbol = std::get<SingleSymbol>(*spec.defaultStyle).symbol;
    REQUIRE(symbol.fill == Color{1.0f, 0.0f, 0.0f, 1.0f});
    REQUIRE(symbol.polygonStroke == Color{0.0f, 0.0f, 1.0f, 1.0f});
    REQUIRE(symbol.lineStroke.g == 1.0f);
    REQUIRE(symbol.lineStroke.a < 1.0f);  // 0x80 -> ~0.502
}

TEST_CASE("Style JSON leaves unnamed symbol keys at their defaults", "[style]") {
    const StyleSpec spec = parseStyleSpec(R"({ "default": { "symbol": { "fill": "#123456" } } })");
    const Symbol& symbol = std::get<SingleSymbol>(*spec.defaultStyle).symbol;

    REQUIRE(symbol.fill == Color{0x12 / 255.0f, 0x34 / 255.0f, 0x56 / 255.0f, 1.0f});
    REQUIRE(symbol.lineStrokeWidth == Symbol{}.lineStrokeWidth);
    REQUIRE(symbol.pointRadius == Symbol{}.pointRadius);
}

TEST_CASE("Style JSON parses a categorized layer", "[style]") {
    const StyleSpec spec = parseStyleSpec(R"({
        "layers": {
            "roads": {
                "type": "categorized",
                "field": "RTTYP",
                "categories": [
                    { "value": "I", "symbol": { "line": "#e04a2f", "lineWidth": 2.5 } },
                    { "value": 3,   "symbol": { "line": "#f2a33c" } }
                ],
                "fallback": { "line": "#9aa0a6", "lineWidth": 0.8 }
            }
        }
    })");

    const auto& categorized = std::get<Categorized>(spec.byLayerName.at("roads"));
    REQUIRE(categorized.field == "RTTYP");
    REQUIRE(categorized.categories.size() == 2);
    REQUIRE(std::get<std::string>(categorized.categories[0].value) == "I");
    REQUIRE(categorized.categories[0].symbol.lineStrokeWidth == 2.5f);
    REQUIRE(std::get<std::int64_t>(categorized.categories[1].value) == 3);
    REQUIRE(categorized.fallback.lineStrokeWidth == 0.8f);
}

TEST_CASE("Style JSON parses a graduated layer", "[style]") {
    const StyleSpec spec = parseStyleSpec(R"({
        "layers": {
            "countries": {
                "type": "graduated",
                "field": "POP_EST",
                "breaks": [
                    { "max": 1000000,  "symbol": { "fill": "#f7fbff" } },
                    { "max": 10000000, "symbol": { "fill": "#6baed6" } }
                ],
                "fallback": { "fill": "#08306b" }
            }
        }
    })");

    const auto& graduated = std::get<Graduated>(spec.byLayerName.at("countries"));
    REQUIRE(graduated.field == "POP_EST");
    REQUIRE(graduated.breaks.size() == 2);
    REQUIRE(graduated.breaks[0].upperBound == 1000000.0);
    REQUIRE(graduated.breaks[1].upperBound == 10000000.0);
}

TEST_CASE("Style JSON rejects malformed input", "[style]") {
    using Catch::Matchers::ContainsSubstring;

    REQUIRE_THROWS_AS(parseStyleSpec("{ not json"), StyleError);
    REQUIRE_THROWS_AS(parseStyleSpec(R"({ "layers": { "a": { "type": "rainbow" } } })"), StyleError);
    REQUIRE_THROWS_AS(parseStyleSpec(R"({ "layres": {} })"), StyleError);  // typo'd top-level key
    REQUIRE_THROWS_AS(parseStyleSpec(R"({ "default": { "symbol": { "linewidth": 2 } } })"),
                      StyleError);  // typo'd symbol key
    REQUIRE_THROWS_AS(parseStyleSpec(R"({ "default": { "symbol": { "fill": "3366cc" } } })"),
                      StyleError);  // missing '#'
    REQUIRE_THROWS_AS(parseStyleSpec(R"({ "default": { "symbol": { "fill": "#zzzzzz" } } })"), StyleError);
    REQUIRE_THROWS_AS(parseStyleSpec(R"({ "default": { "symbol": { "lineWidth": -1 } } })"), StyleError);

    // Breaks are matched in order, so an unsorted list would silently
    // misclassify rather than fail - reject it at parse time instead.
    REQUIRE_THROWS_WITH(parseStyleSpec(R"({
        "layers": { "a": { "type": "graduated", "field": "x",
                            "breaks": [ { "max": 10 }, { "max": 5 } ] } }
    })"),
                         ContainsSubstring("ascending"));
}

TEST_CASE("Loading a nonexistent style file throws", "[style]") {
    REQUIRE_THROWS_AS(loadStyleSpec(fixturePath("no_such_style.json")), StyleError);
}
