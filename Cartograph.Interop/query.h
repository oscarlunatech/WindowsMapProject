#pragma once

#include "map.h"
#include "types.h"

namespace Cartograph {
namespace Interop {

// cartograph::query::identify. A static class rather than a namespace-level
// function, because C# has no way to call a free function.
public ref class Query abstract sealed {
public:
    // Every feature within tolerance (map units) of mapPoint, ordered topmost
    // layer first and then nearest first - the order an identify panel should
    // list them in. Hidden layers are skipped.
    //
    // Tolerance is in **map units**, not pixels, because Core has no screen.
    // A shell converts a pixel radius with viewport.Scale, the way Viewer
    // does: a 5px target divided by Scale stays 5px at every zoom.
    static array<IdentifyHit> ^ Identify(Map ^ map, MapPoint mapPoint, double tolerance);
};

}  // namespace Interop
}  // namespace Cartograph
