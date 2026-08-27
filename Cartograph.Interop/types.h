#pragma once

// The value types that cross the boundary by copy.
//
// Every one of these mirrors a small, cheap, immutable Core struct
// (cartograph::Point2D, Envelope, style::Color, render::ScreenSize, ...).
// They are managed *value* types rather than ref classes on purpose: they are
// a few doubles each, they are copied constantly by the shell's data binding,
// and giving each one a GC allocation and a finalizer would be pure overhead
// for no ownership benefit. Anything that owns a native resource is a ref
// class instead - see map.h, geometry.h, viewport.h, stylesheet.h.

namespace Cartograph {
namespace Interop {

// Mirrors cartograph::GeometryType.
public enum class GeometryType {
    Point,
    LineString,
    Polygon,
    MultiPoint,
    MultiLineString,
    MultiPolygon,
    Unknown
};

// Mirrors cartograph::FieldType.
public enum class FieldType {
    Integer,
    Integer64,
    Real,
    String,
    Unknown
};

// A coordinate. Whether it is in map units or screen pixels depends on which
// side of Viewport it came from - Core makes the same distinction by context
// rather than by type, and inventing two types here would mean converting
// between them at every call.
public value struct MapPoint {
    double X;
    double Y;

    MapPoint(double x, double y) {
        X = x;
        Y = y;
    }

    virtual System::String ^ ToString() override {
        return System::String::Format("({0}, {1})", X, Y);
    }
};

// Mirrors cartograph::Envelope. IsValid is false for the extent of an empty
// layer or an empty map; the bounds are meaningless when it is.
public value struct Envelope {
    double MinX;
    double MinY;
    double MaxX;
    double MaxY;
    bool IsValid;

    Envelope(double minX, double minY, double maxX, double maxY, bool isValid) {
        MinX = minX;
        MinY = minY;
        MaxX = maxX;
        MaxY = maxY;
        IsValid = isValid;
    }

    property double Width {
        double get() { return MaxX - MinX; }
    }

    property double Height {
        double get() { return MaxY - MinY; }
    }

    property MapPoint Center {
        MapPoint get() { return MapPoint((MinX + MaxX) * 0.5, (MinY + MaxY) * 0.5); }
    }
};

// Mirrors cartograph::render::ScreenSize.
public value struct ScreenSize {
    int Width;
    int Height;

    ScreenSize(int width, int height) {
        Width = width;
        Height = height;
    }
};

// Mirrors cartograph::style::Color: straight (non-premultiplied) RGBA, each
// channel 0..1. Deliberately *not* System::Windows::Media::Color - that would
// drag PresentationCore into this assembly and tie the boundary to WPF, when
// the shell can convert in one line.
public value struct Color {
    float R;
    float G;
    float B;
    float A;

    Color(float r, float g, float b, float a) {
        R = r;
        G = g;
        B = b;
        A = a;
    }
};

// Mirrors cartograph::style::Symbol - all three geometry classes at once, for
// the reasons given in symbol.h.
public value struct Symbol {
    Color Fill;
    Color PolygonStroke;
    float PolygonStrokeWidth;

    Color LineStroke;
    float LineStrokeWidth;

    Color PointFill;
    float PointRadius;
};

// Mirrors cartograph::FieldDef.
public value struct FieldDefinition {
    System::String ^ Name;
    FieldType Type;

    FieldDefinition(System::String ^ name, FieldType type) {
        Name = name;
        Type = type;
    }
};

// Mirrors cartograph::query::Hit.
//
// The two shapes a hit can take are kept in one type exactly as Core keeps
// them: a vector hit fills FeatureIndex and Distance, a raster hit fills
// BandValues instead. IsRaster says which, on the same rule Core uses.
public value struct IdentifyHit {
    int LayerIndex;

    // Vector hit. Meaningless when IsRaster is true.
    int FeatureIndex;
    double Distance;

    // Raster hit: the raw, unstretched value of every band at that point.
    // Non-empty exactly when this hit came from a raster layer.
    array<double> ^ BandValues;

    property bool IsRaster {
        bool get() { return BandValues != nullptr && BandValues->Length > 0; }
    }
};

}  // namespace Interop
}  // namespace Cartograph
