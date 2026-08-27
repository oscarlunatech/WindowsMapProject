#include "conversions.h"

#include <cstring>
#include <variant>

using namespace System;

namespace Cartograph {
namespace Interop {
namespace Detail {

std::string ToNative(String ^ text) {
    if (text == nullptr || text->Length == 0) {
        return std::string();
    }
    array<Byte> ^ bytes = Text::Encoding::UTF8->GetBytes(text);
    if (bytes->Length == 0) {
        return std::string();
    }
    std::string result(static_cast<std::size_t>(bytes->Length), '\0');
    pin_ptr<Byte> pinned = &bytes[0];
    std::memcpy(result.data(), pinned, result.size());
    return result;
}

String ^ ToManaged(const std::string& text) {
    if (text.empty()) {
        return String::Empty;
    }
    array<Byte> ^ bytes = gcnew array<Byte>(static_cast<int>(text.size()));
    pin_ptr<Byte> pinned = &bytes[0];
    std::memcpy(pinned, text.data(), text.size());
    return Text::Encoding::UTF8->GetString(bytes);
}

String ^ ToManaged(const char* text) {
    return text == nullptr ? String::Empty : ToManaged(std::string(text));
}

int ToInt(std::size_t value) {
    if (value > static_cast<std::size_t>(Int32::MaxValue)) {
        throw gcnew OverflowException("Value does not fit in a 32-bit index.");
    }
    return static_cast<int>(value);
}

std::size_t ToIndex(int value, std::size_t count, String ^ parameter) {
    if (value < 0 || static_cast<std::size_t>(value) >= count) {
        throw gcnew ArgumentOutOfRangeException(
            parameter, value,
            String::Format("Index must be in [0, {0}).", ToInt(count)));
    }
    return static_cast<std::size_t>(value);
}

MapPoint ToManaged(const cartograph::Point2D& point) {
    return MapPoint(point.x, point.y);
}

cartograph::Point2D ToNative(MapPoint point) {
    return cartograph::Point2D{point.X, point.Y};
}

Envelope ToManaged(const cartograph::Envelope& envelope) {
    return Envelope(envelope.minX, envelope.minY, envelope.maxX, envelope.maxY, envelope.valid);
}

cartograph::Envelope ToNative(Envelope envelope) {
    cartograph::Envelope result;
    result.minX = envelope.MinX;
    result.minY = envelope.MinY;
    result.maxX = envelope.MaxX;
    result.maxY = envelope.MaxY;
    result.valid = envelope.IsValid;
    return result;
}

ScreenSize ToManaged(const cartograph::render::ScreenSize& size) {
    return ScreenSize(size.width, size.height);
}

cartograph::render::ScreenSize ToNative(ScreenSize size) {
    cartograph::render::ScreenSize result;
    result.width = size.Width;
    result.height = size.Height;
    return result;
}

Color ToManaged(const cartograph::style::Color& color) {
    return Color(color.r, color.g, color.b, color.a);
}

Symbol ToManaged(const cartograph::style::Symbol& symbol) {
    Symbol result;
    result.Fill = ToManaged(symbol.fill);
    result.PolygonStroke = ToManaged(symbol.polygonStroke);
    result.PolygonStrokeWidth = symbol.polygonStrokeWidth;
    result.LineStroke = ToManaged(symbol.lineStroke);
    result.LineStrokeWidth = symbol.lineStrokeWidth;
    result.PointFill = ToManaged(symbol.pointFill);
    result.PointRadius = symbol.pointRadius;
    return result;
}

GeometryType ToManaged(cartograph::GeometryType type) {
    switch (type) {
        case cartograph::GeometryType::Point:           return GeometryType::Point;
        case cartograph::GeometryType::LineString:      return GeometryType::LineString;
        case cartograph::GeometryType::Polygon:         return GeometryType::Polygon;
        case cartograph::GeometryType::MultiPoint:      return GeometryType::MultiPoint;
        case cartograph::GeometryType::MultiLineString: return GeometryType::MultiLineString;
        case cartograph::GeometryType::MultiPolygon:    return GeometryType::MultiPolygon;
        default:                                        return GeometryType::Unknown;
    }
}

FieldType ToManaged(cartograph::FieldType type) {
    switch (type) {
        case cartograph::FieldType::Integer:   return FieldType::Integer;
        case cartograph::FieldType::Integer64: return FieldType::Integer64;
        case cartograph::FieldType::Real:      return FieldType::Real;
        case cartograph::FieldType::String:    return FieldType::String;
        default:                               return FieldType::Unknown;
    }
}

FieldDefinition ToManaged(const cartograph::FieldDef& field) {
    return FieldDefinition(ToManaged(field.name), ToManaged(field.type));
}

Object ^ ToManaged(const cartograph::AttributeValue& value) {
    if (std::holds_alternative<std::int64_t>(value)) {
        return std::get<std::int64_t>(value);
    }
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value);
    }
    if (std::holds_alternative<std::string>(value)) {
        return ToManaged(std::get<std::string>(value));
    }
    return nullptr;  // std::monostate - the field is null for this feature
}

array<double> ^ ToManaged(const std::vector<double>& values) {
    array<double> ^ result = gcnew array<double>(static_cast<int>(values.size()));
    if (!values.empty()) {
        pin_ptr<double> pinned = &result[0];
        std::memcpy(pinned, values.data(), values.size() * sizeof(double));
    }
    return result;
}

IdentifyHit ToManaged(const cartograph::query::Hit& hit) {
    IdentifyHit result;
    result.LayerIndex = ToInt(hit.layerIndex);
    result.FeatureIndex = ToInt(hit.featureIndex);
    result.Distance = hit.distance;
    result.BandValues = ToManaged(hit.bandValues);
    return result;
}

}  // namespace Detail
}  // namespace Interop
}  // namespace Cartograph
