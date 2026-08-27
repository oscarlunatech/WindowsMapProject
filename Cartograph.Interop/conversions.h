#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "cartograph/geometry.h"
#include "cartograph/layer.h"
#include "cartograph/query/identify.h"
#include "cartograph/render/viewport.h"
#include "cartograph/style/symbol.h"

#include "types.h"

// Internal plumbing: native <-> managed conversion for the value types in
// types.h. Not part of the assembly's public surface (everything here is in
// Detail and nothing is `public`), so the shell never sees it.

namespace Cartograph {
namespace Interop {
namespace Detail {

// Strings cross this boundary as **UTF-8**, spelled out explicitly in both
// directions.
//
// This is the one conversion in the assembly that is easy to get quietly
// wrong. msclr::interop::marshal_as<std::string> is the obvious-looking choice
// and it encodes with the process ANSI code page, which mangles any non-ASCII
// path or attribute value into '?'. GDAL hands Core UTF-8 and expects UTF-8
// back - a Natural Earth place name like "Côte d'Ivoire" round-trips only if
// both directions agree on that, so both directions say so.
std::string ToNative(System::String ^ text);
System::String ^ ToManaged(const std::string& text);
System::String ^ ToManaged(const char* text);

// Core counts and indexes with std::size_t; .NET indexes with int, which is
// what every collection, data-binding and ListView the shell will use expects.
// The narrowing is checked rather than assumed: a map with more than 2^31
// features is not something this codebase can currently produce, but silently
// wrapping to a negative index if one ever appears is worse than throwing.
int ToInt(std::size_t value);

// A managed index back to a Core one, bounds-checked against `count`. Throws
// ArgumentOutOfRangeException naming `parameter`, which is what a .NET caller
// expects from a bad index - rather than letting a negative or oversized value
// reach a std::vector::operator[] and corrupt memory.
std::size_t ToIndex(int value, std::size_t count, System::String ^ parameter);

MapPoint ToManaged(const cartograph::Point2D& point);
cartograph::Point2D ToNative(MapPoint point);

Envelope ToManaged(const cartograph::Envelope& envelope);
cartograph::Envelope ToNative(Envelope envelope);

ScreenSize ToManaged(const cartograph::render::ScreenSize& size);
cartograph::render::ScreenSize ToNative(ScreenSize size);

Color ToManaged(const cartograph::style::Color& color);
Symbol ToManaged(const cartograph::style::Symbol& symbol);

GeometryType ToManaged(cartograph::GeometryType type);
FieldType ToManaged(cartograph::FieldType type);

FieldDefinition ToManaged(const cartograph::FieldDef& field);

// An attribute becomes the natural .NET shape of its variant alternative:
// monostate -> nullptr, int64_t -> Int64, double -> Double, string -> String.
// A DataGrid binds all four without help, and `is System.String` reads better
// on the C# side than a discriminator enum would.
System::Object ^ ToManaged(const cartograph::AttributeValue& value);

array<double> ^ ToManaged(const std::vector<double>& values);

IdentifyHit ToManaged(const cartograph::query::Hit& hit);

}  // namespace Detail
}  // namespace Interop
}  // namespace Cartograph
