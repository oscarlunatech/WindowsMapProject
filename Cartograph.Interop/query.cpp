#include "query.h"

#include <vector>

#include "cartograph/query/identify.h"

#include "conversions.h"
#include "errors.h"

using namespace System;

namespace Cartograph {
namespace Interop {

array<IdentifyHit> ^ Query::Identify(Map ^ map, MapPoint mapPoint, double tolerance) {
    if (map == nullptr) {
        throw gcnew ArgumentNullException("map");
    }
    cartograph::Map& nativeMap = map->Native();
    const cartograph::Point2D point = Detail::ToNative(mapPoint);

    CARTOGRAPH_TRY
        const std::vector<cartograph::query::Hit> hits =
            cartograph::query::identify(nativeMap, point, tolerance);

        array<IdentifyHit> ^ result = gcnew array<IdentifyHit>(Detail::ToInt(hits.size()));
        for (std::size_t i = 0; i < hits.size(); ++i) {
            result[static_cast<int>(i)] = Detail::ToManaged(hits[i]);
        }
        return result;
    CARTOGRAPH_CATCH
}

}  // namespace Interop
}  // namespace Cartograph
