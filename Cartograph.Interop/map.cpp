#include "map.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "conversions.h"
#include "errors.h"

using namespace System;

namespace Cartograph {
namespace Interop {
namespace {

// paths arrive as a managed array and Core wants a vector<string>; done in one
// place because both Open overloads need it and both must reject a null array
// with an ArgumentNullException rather than an access violation.
std::vector<std::string> ToNativePaths(array<String ^> ^ paths) {
    if (paths == nullptr) {
        throw gcnew ArgumentNullException("paths");
    }
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(paths->Length));
    for (int i = 0; i < paths->Length; ++i) {
        result.push_back(Detail::ToNative(paths[i]));
    }
    return result;
}

}  // namespace

// --- JobPool ----------------------------------------------------------------

JobPool::JobPool() : native_(new cartograph::jobs::ThreadPool()) {}

JobPool::JobPool(int threadCount)
    : native_(new cartograph::jobs::ThreadPool(
          static_cast<std::size_t>((std::max)(1, threadCount)))) {}

JobPool::!JobPool() {
    delete native_;
    native_ = nullptr;
}

cartograph::jobs::ThreadPool& JobPool::Native() {
    if (native_ == nullptr) {
        throw gcnew ObjectDisposedException("JobPool");
    }
    return *native_;
}

int JobPool::ThreadCount::get() {
    return Detail::ToInt(Native().size());
}

// --- Map --------------------------------------------------------------------

Map ^ Map::Open(array<String ^> ^ paths, String ^ displayCrs) {
    const std::vector<std::string> nativePaths = ToNativePaths(paths);
    const std::string crs = Detail::ToNative(displayCrs);
    CARTOGRAPH_TRY
        return gcnew Map(new cartograph::Map(cartograph::Map::open(nativePaths, crs)));
    CARTOGRAPH_CATCH
}

Map ^ Map::Open(array<String ^> ^ paths, String ^ displayCrs, JobPool ^ pool) {
    if (pool == nullptr) {
        throw gcnew ArgumentNullException("pool");
    }
    const std::vector<std::string> nativePaths = ToNativePaths(paths);
    const std::string crs = Detail::ToNative(displayCrs);
    cartograph::jobs::ThreadPool& nativePool = pool->Native();
    CARTOGRAPH_TRY
        return gcnew Map(new cartograph::Map(cartograph::Map::open(nativePaths, crs, nativePool)));
    CARTOGRAPH_CATCH
}

String ^ Map::DefaultDisplayCrs::get() {
    return Detail::ToManaged(cartograph::Dataset::defaultDisplayCrs());
}

Map::!Map() {
    delete native_;
    native_ = nullptr;
}

cartograph::Map& Map::Native() {
    if (native_ == nullptr) {
        throw gcnew ObjectDisposedException("Map");
    }
    return *native_;
}

String ^ Map::DisplayCrs::get() {
    return Detail::ToManaged(Native().displayCrs());
}

void Map::SetDisplayCrs(String ^ displayCrs) {
    const std::string crs = Detail::ToNative(displayCrs);
    cartograph::Map& map = Native();
    CARTOGRAPH_TRY
        map.setDisplayCrs(crs);
    CARTOGRAPH_CATCH
}

void Map::SetDisplayCrs(String ^ displayCrs, JobPool ^ pool) {
    if (pool == nullptr) {
        throw gcnew ArgumentNullException("pool");
    }
    const std::string crs = Detail::ToNative(displayCrs);
    cartograph::Map& map = Native();
    cartograph::jobs::ThreadPool& nativePool = pool->Native();
    CARTOGRAPH_TRY
        map.setDisplayCrs(crs, nativePool);
    CARTOGRAPH_CATCH
}

int Map::LayerCount::get() {
    return Detail::ToInt(Native().layers().size());
}

MapLayer ^ Map::GetLayer(int index) {
    Detail::ToIndex(index, Native().layers().size(), "index");
    return gcnew MapLayer(this, index);
}

Envelope Map::Extent::get() {
    cartograph::Map& map = Native();
    CARTOGRAPH_TRY
        return Detail::ToManaged(map.extent());
    CARTOGRAPH_CATCH
}

Int64 Map::FeatureCount::get() {
    return static_cast<Int64>(Native().featureCount());
}

bool Map::IsEmpty::get() {
    return Native().empty();
}

// --- MapLayer ---------------------------------------------------------------

MapLayer::MapLayer(Map ^ owner, int index) : owner_(owner), index_(index) {}

const cartograph::MapLayer& MapLayer::native() {
    return nativeMutable();
}

cartograph::MapLayer& MapLayer::nativeMutable() {
    std::vector<cartograph::MapLayer>& layers = owner_->Native().layers();
    // Re-checked on every access rather than once at construction: the map can
    // be reprojected (or, from Phase 16, edited) between the handle being made
    // and being used, and an out-of-range index must surface as an exception
    // rather than as a read past the end of the vector.
    const std::size_t index = Detail::ToIndex(index_, layers.size(), "index");
    return layers[index];
}

const cartograph::Layer& MapLayer::vector() {
    const cartograph::MapLayer& layer = native();
    if (layer.isRaster()) {
        throw gcnew InvalidOperationException(
            String::Format("Layer '{0}' is a raster layer; this member is for vector layers only.",
                           Detail::ToManaged(layer.name())));
    }
    return layer.layer();
}

const cartograph::raster::RasterSource& MapLayer::raster() {
    const cartograph::MapLayer& layer = native();
    if (!layer.isRaster()) {
        throw gcnew InvalidOperationException(
            String::Format("Layer '{0}' is a vector layer; this member is for raster layers only.",
                           Detail::ToManaged(layer.name())));
    }
    return *layer.rasterSource();
}

String ^ MapLayer::Name::get() {
    return Detail::ToManaged(native().name());
}

String ^ MapLayer::SourcePath::get() {
    return Detail::ToManaged(native().sourcePath());
}

bool MapLayer::IsRaster::get() {
    return native().isRaster();
}

Envelope MapLayer::Extent::get() {
    CARTOGRAPH_TRY
        return Detail::ToManaged(native().extent());
    CARTOGRAPH_CATCH
}

bool MapLayer::Visible::get() {
    return native().visible();
}

void MapLayer::Visible::set(bool value) {
    nativeMutable().setVisible(value);
}

float MapLayer::Opacity::get() {
    return native().opacity();
}

void MapLayer::Opacity::set(float value) {
    nativeMutable().setOpacity(value);
}

int MapLayer::FeatureCount::get() {
    return Detail::ToInt(vector().features().size());
}

int MapLayer::FieldCount::get() {
    return Detail::ToInt(vector().fields().size());
}

FieldDefinition MapLayer::GetField(int fieldIndex) {
    const cartograph::Layer& layer = vector();
    const std::size_t field = Detail::ToIndex(fieldIndex, layer.fields().size(), "fieldIndex");
    return Detail::ToManaged(layer.fields()[field]);
}

String ^ MapLayer::CrsWkt::get() {
    return Detail::ToManaged(vector().crsWkt());
}

Int64 MapLayer::GetFeatureId(int featureIndex) {
    const cartograph::Layer& layer = vector();
    const std::size_t feature = Detail::ToIndex(featureIndex, layer.features().size(), "featureIndex");
    return layer.features()[feature].id();
}

Object ^ MapLayer::GetAttribute(int featureIndex, int fieldIndex) {
    const cartograph::Layer& layer = vector();
    const std::size_t feature = Detail::ToIndex(featureIndex, layer.features().size(), "featureIndex");
    const std::vector<cartograph::AttributeValue>& attributes = layer.features()[feature].attributes();
    const std::size_t field = Detail::ToIndex(fieldIndex, attributes.size(), "fieldIndex");
    return Detail::ToManaged(attributes[field]);
}

Geometry ^ MapLayer::GetFeatureGeometry(int featureIndex) {
    const cartograph::Layer& layer = vector();
    const std::size_t feature = Detail::ToIndex(featureIndex, layer.features().size(), "featureIndex");
    return gcnew Geometry(layer.features()[feature].geometry());
}

int MapLayer::BandCount::get() {
    return raster().bandCount();
}

array<double> ^ MapLayer::SampleBands(MapPoint mapPoint) {
    const cartograph::raster::RasterSource& source = raster();
    const cartograph::Point2D point = Detail::ToNative(mapPoint);
    CARTOGRAPH_TRY
        return Detail::ToManaged(source.sample(point));
    CARTOGRAPH_CATCH
}

}  // namespace Interop
}  // namespace Cartograph
