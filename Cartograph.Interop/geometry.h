#pragma once

#include "cartograph/geometry.h"

#include "types.h"

namespace Cartograph {
namespace Interop {

// One feature's geometry, exposed in Core's own data-oriented shape: parts,
// each of which is a list of rings, each of which is a list of points. See
// cartograph/geometry.h - a part is one ring for a point or line, and an
// exterior ring followed by hole rings for a polygon.
//
// This is a **copy** taken at the moment MapLayer::FeatureGeometry was called,
// not a view onto the layer. Copying a large geometry is not free, which is
// why nothing on the drawing path goes through here: the renderer walks Core's
// geometry natively and only pixels cross the boundary. This exists for the
// attribute table, for identify results, and for the editing tools Phase 17
// will need - all of which touch one feature at a time.
//
// Rings come back as fresh managed arrays per call rather than as a cached
// jagged array, so a caller that walks a big polygon repeatedly should hold
// onto what Ring() returns.
public ref class Geometry sealed {
public:
    ~Geometry() { this->!Geometry(); }
    !Geometry();

    property GeometryType Type {
        GeometryType get();
    }

    property Envelope Extent {
        Envelope get();
    }

    // Multi* geometries have more than one part; everything else has one (or
    // zero, if it is empty).
    property int PartCount {
        int get();
    }

    // Rings in one part: 1 for a point or line, 1 + hole count for a polygon.
    int RingCount(int partIndex);

    // The points of one ring, in order.
    array<MapPoint> ^ Ring(int partIndex, int ringIndex);

internal:
    Geometry(const cartograph::Geometry& geometry);

private:
    // Raw pointer rather than unique_ptr: a managed type cannot hold a native
    // object with a destructor by value, so ownership is expressed the way
    // .NET expects instead - the destructor above is IDisposable::Dispose and
    // the finalizer below is the backstop if a caller forgets.
    cartograph::Geometry* native_;

    cartograph::Geometry& native();
};

}  // namespace Interop
}  // namespace Cartograph
