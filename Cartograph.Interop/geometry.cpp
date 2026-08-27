#include "geometry.h"

#include "conversions.h"
#include "errors.h"

using namespace System;

namespace Cartograph {
namespace Interop {

Geometry::Geometry(const cartograph::Geometry& geometry) : native_(new cartograph::Geometry(geometry)) {}

Geometry::!Geometry() {
    delete native_;
    native_ = nullptr;
}

cartograph::Geometry& Geometry::native() {
    if (native_ == nullptr) {
        throw gcnew ObjectDisposedException("Geometry");
    }
    return *native_;
}

GeometryType Geometry::Type::get() {
    return Detail::ToManaged(native().type());
}

Envelope Geometry::Extent::get() {
    CARTOGRAPH_TRY
        return Detail::ToManaged(native().extent());
    CARTOGRAPH_CATCH
}

int Geometry::PartCount::get() {
    return Detail::ToInt(native().parts().size());
}

int Geometry::RingCount(int partIndex) {
    const cartograph::Geometry& geometry = native();
    const std::size_t part = Detail::ToIndex(partIndex, geometry.parts().size(), "partIndex");
    return Detail::ToInt(geometry.parts()[part].size());
}

array<MapPoint> ^ Geometry::Ring(int partIndex, int ringIndex) {
    const cartograph::Geometry& geometry = native();
    const std::size_t part = Detail::ToIndex(partIndex, geometry.parts().size(), "partIndex");
    const cartograph::Part& parts = geometry.parts()[part];
    const std::size_t ring = Detail::ToIndex(ringIndex, parts.size(), "ringIndex");
    const cartograph::Ring& points = parts[ring];

    array<MapPoint> ^ result = gcnew array<MapPoint>(Detail::ToInt(points.size()));
    for (std::size_t i = 0; i < points.size(); ++i) {
        result[static_cast<int>(i)] = Detail::ToManaged(points[i]);
    }
    return result;
}

}  // namespace Interop
}  // namespace Cartograph
