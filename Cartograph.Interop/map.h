#pragma once

#include "cartograph/jobs/thread_pool.h"
#include "cartograph/map.h"

#include "geometry.h"
#include "types.h"

namespace Cartograph {
namespace Interop {

ref class Map;

// cartograph::jobs::ThreadPool. The shell owns one for the lifetime of the
// window and hands it to every call that takes one, exactly as Viewer does.
//
// The no-nested-submission rule from thread_pool.h crosses the boundary
// unchanged: a call that takes a JobPool must not be made from a thread that
// is already running work on that same pool, or a fixed-size pool can
// deadlock. In practice this means the shell's background loads use the pool,
// and never fan out from inside one.
public ref class JobPool sealed {
public:
    // One worker per hardware thread.
    JobPool();
    // Explicit worker count; clamped to at least 1.
    JobPool(int threadCount);

    ~JobPool() { this->!JobPool(); }
    !JobPool();

    property int ThreadCount {
        int get();
    }

internal:
    cartograph::jobs::ThreadPool& Native();

private:
    cartograph::jobs::ThreadPool* native_;
};

// One layer of a Map: a **handle**, not a copy.
//
// It holds the owning Map and an index rather than a pointer into
// Map::layers(), which matters because Map::SetDisplayCrs replaces every layer
// wholesale (it re-reads the source files - see map.h in Core). A cached
// native pointer would dangle across that call; an index does not, because the
// reprojected map has the same layers in the same order. Every access resolves
// through the owner and is bounds-checked, so the worst a stale handle can do
// is throw.
//
// Vector-only members throw InvalidOperationException on a raster layer and
// vice versa, mirroring Core's "calling these on the wrong kind is a
// programming error; check IsRaster first" rule - except that Core says that
// in a comment and this says it with an exception, since a managed caller
// cannot be trusted with undefined behaviour.
public ref class MapLayer sealed {
public:
    // The layer name for a vector layer, the file's stem for a raster one.
    property System::String ^ Name {
        System::String ^ get();
    }

    // The file this layer was read from. Several layers can share one path -
    // a single vector dataset often contains many.
    property System::String ^ SourcePath {
        System::String ^ get();
    }

    property bool IsRaster {
        bool get();
    }

    // Extent in the map's display CRS, whichever kind of layer this is.
    property Envelope Extent {
        Envelope get();
    }

    // Hidden layers are skipped by drawing *and* by identify.
    property bool Visible {
        bool get();
        void set(bool value);
    }

    // 0 (transparent) to 1 (opaque); values outside that range are clamped.
    property float Opacity {
        float get();
        void set(float value);
    }

    // --- Vector layers only ---

    property int FeatureCount {
        int get();
    }

    property int FieldCount {
        int get();
    }

    FieldDefinition GetField(int fieldIndex);

    // The CRS this layer's geometry is actually in, or empty if the source
    // file carried no CRS metadata to reproject from.
    property System::String ^ CrsWkt {
        System::String ^ get();
    }

    // The feature's own id from the source file, which is not its index.
    System::Int64 GetFeatureId(int featureIndex);

    // Null, Int64, Double or String - the natural .NET shape of Core's
    // AttributeValue variant. A DataGrid binds all four unaided.
    System::Object ^ GetAttribute(int featureIndex, int fieldIndex);

    // A copy of the feature's geometry; see Geometry's remarks on cost.
    Geometry ^ GetFeatureGeometry(int featureIndex);

    // --- Raster layers only ---

    property int BandCount {
        int get();
    }

    // Raw (unstretched) value of every band at one map coordinate. Empty if
    // the point falls outside the raster.
    array<double> ^ SampleBands(MapPoint mapPoint);

internal:
    MapLayer(Map ^ owner, int index);

private:
    Map ^ owner_;
    int index_;

    const cartograph::MapLayer& native();
    cartograph::MapLayer& nativeMutable();
    const cartograph::Layer& vector();  // throws on a raster layer
    const cartograph::raster::RasterSource& raster();  // throws on a vector layer
};

// cartograph::Map: an ordered stack of layers drawn bottom-to-top, so
// GetLayer(0) is underneath and GetLayer(LayerCount - 1) is on top.
public ref class Map sealed {
public:
    // Opens each path and appends its layers in order. Each path is tried as
    // vector first and as raster if that fails. Every layer is reprojected to
    // displayCrs as it loads.
    //
    // The JobPool overload builds the expensive per-layer vector caches
    // concurrently and is what the shell should use; the one without is for
    // callers with no pool handy. Throws DatasetOpenException if a path cannot
    // be read either way.
    static Map ^ Open(array<System::String ^> ^ paths, System::String ^ displayCrs);
    static Map ^ Open(array<System::String ^> ^ paths, System::String ^ displayCrs, JobPool ^ pool);

    // EPSG:3857 (Web Mercator) - what Core defaults to, and what web maps and
    // XYZ basemap tiles use.
    static property System::String ^ DefaultDisplayCrs {
        System::String ^ get();
    }

    ~Map() { this->!Map(); }
    !Map();

    // The CRS every layer's coordinates are in.
    property System::String ^ DisplayCrs {
        System::String ^ get();
    }

    // Reprojects the whole map by re-reading every source file in the new CRS.
    // Strongly exception-safe: on failure the map is left untouched. Per-layer
    // visibility and opacity are preserved and layer order is unchanged, so
    // MapLayer handles stay valid across the call - but **any Stylesheet built
    // against this map must be rebuilt**, since the layers are replaced.
    void SetDisplayCrs(System::String ^ displayCrs);
    void SetDisplayCrs(System::String ^ displayCrs, JobPool ^ pool);

    property int LayerCount {
        int get();
    }

    MapLayer ^ GetLayer(int index);

    // Union of *every* layer's extent, hidden ones included - what a "zoom to
    // full extent" should show.
    property Envelope Extent {
        Envelope get();
    }

    // Total vector features across every layer. Raster layers contribute
    // nothing.
    property System::Int64 FeatureCount {
        System::Int64 get();
    }

    property bool IsEmpty {
        bool get();
    }

internal:
    cartograph::Map& Native();

private:
    Map(cartograph::Map* native) : native_(native) {}

    cartograph::Map* native_;
};

}  // namespace Interop
}  // namespace Cartograph
