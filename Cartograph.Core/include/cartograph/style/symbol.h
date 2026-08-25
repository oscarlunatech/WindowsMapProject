#pragma once

namespace cartograph::style {

// Straight RGBA, each channel 0..1 - deliberately matching D2D1::ColorF's
// convention so renderer.cpp can hand one to CreateSolidColorBrush unchanged,
// without Cartograph.Core's public headers naming a D2D type.
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    friend bool operator==(const Color&, const Color&) = default;
};

// How one feature is drawn.
//
// Carries all three geometry classes at once rather than a single generic
// fill/stroke pair, for two reasons: a Symbol has to be able to express the
// pre-Phase-7 hardcoded styling exactly (gray polygons, black outlines, blue
// lines, red points), and drawDatasetCulled already batches a layer's points,
// lines and polygons into separate draw calls anyway - so it wants all three
// in hand at the same time.
//
// The defaults below are exactly the values renderer.cpp hardcoded before
// Phase 7. That is what keeps Stylesheet::defaults() - and therefore the
// golden-image test - byte-for-byte unchanged by this phase.
struct Symbol {
    Color fill{0.85f, 0.85f, 0.85f, 1.0f};
    Color polygonStroke{0.0f, 0.0f, 0.0f, 1.0f};
    float polygonStrokeWidth = 1.0f;  // 0 draws no polygon outline at all

    Color lineStroke{0.0f, 0.2f, 0.8f, 1.0f};
    float lineStrokeWidth = 1.5f;  // 0 draws no lines at all

    Color pointFill{1.0f, 0.0f, 0.0f, 1.0f};
    float pointRadius = 3.0f;  // 0 draws no points at all

    friend bool operator==(const Symbol&, const Symbol&) = default;
};

}  // namespace cartograph::style
