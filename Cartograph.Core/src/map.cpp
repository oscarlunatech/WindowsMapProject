#include "cartograph/map.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cartograph {

namespace {

// Carries per-layer display state across a reload, matched by position. The
// stack shape can't change - it's the same files read the same way - so
// index matching is sound.
void restoreDisplayState(const std::vector<MapLayer>& from, std::vector<MapLayer>& to) {
    const std::size_t count = (std::min)(from.size(), to.size());
    for (std::size_t i = 0; i < count; ++i) {
        to[i].setVisible(from[i].visible());
        to[i].setOpacity(from[i].opacity());
    }
}

// "C:\data\hillshade.tif" -> "hillshade". A raster file is one layer and
// carries no layer name of its own, so its stem is the closest thing to one.
std::string stemOf(const std::string& path) {
    const std::size_t lastSlash = path.find_last_of("\\/");
    const std::size_t start = lastSlash == std::string::npos ? 0 : lastSlash + 1;
    const std::size_t lastDot = path.find_last_of('.');
    const std::size_t end = (lastDot == std::string::npos || lastDot < start) ? path.size() : lastDot;
    return path.substr(start, end - start);
}

}  // namespace

MapLayer::MapLayer(Layer layer, render::LayerCache cache, std::string sourcePath)
    : content_(VectorContent{std::move(layer), std::move(cache)}), sourcePath_(std::move(sourcePath)) {}

MapLayer::MapLayer(std::string name, std::shared_ptr<raster::RasterSource> source, std::string sourcePath)
    : content_(RasterContent{std::move(name), std::move(source), {}}),
      sourcePath_(std::move(sourcePath)) {}

const std::string& MapLayer::name() const {
    if (const auto* raster = std::get_if<RasterContent>(&content_)) {
        return raster->name;
    }
    return std::get<VectorContent>(content_).layer.name();
}

Envelope MapLayer::extent() const {
    if (const auto* raster = std::get_if<RasterContent>(&content_)) {
        return raster->source->extent();
    }
    return std::get<VectorContent>(content_).layer.extent();
}

const Layer& MapLayer::layer() const { return std::get<VectorContent>(content_).layer; }

const render::LayerCache& MapLayer::cache() const { return std::get<VectorContent>(content_).cache; }

const std::shared_ptr<raster::RasterSource>& MapLayer::rasterSource() const {
    return std::get<RasterContent>(content_).source;
}

const raster::RasterImage& MapLayer::image() const { return std::get<RasterContent>(content_).image; }

void MapLayer::setImage(raster::RasterImage image) {
    std::get<RasterContent>(content_).image = std::move(image);
}

void MapLayer::setOpacity(float opacity) {
    opacity_ = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
}

void Map::addDataset(Dataset dataset, const std::string& sourcePath) {
    sourcePaths_.push_back(sourcePath);
    std::vector<Layer> layers = dataset.takeLayers();
    layers_.reserve(layers_.size() + layers.size());
    for (Layer& layer : layers) {
        render::LayerCache cache(layer);
        layers_.emplace_back(std::move(layer), std::move(cache), sourcePath);
    }
}

void Map::addDataset(Dataset dataset, const std::string& sourcePath, jobs::ThreadPool& pool) {
    sourcePaths_.push_back(sourcePath);
    std::vector<Layer> layers = dataset.takeLayers();

    // buildLayerCachesParallel holds const references into `layers` while it
    // works, so nothing may move out of that vector until it returns.
    std::vector<render::LayerCache> caches = render::buildLayerCachesParallel(pool, layers);

    layers_.reserve(layers_.size() + layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        layers_.emplace_back(std::move(layers[i]), std::move(caches[i]), sourcePath);
    }
}

void Map::addRaster(std::shared_ptr<raster::RasterSource> source, const std::string& sourcePath) {
    sourcePaths_.push_back(sourcePath);
    layers_.emplace_back(stemOf(sourcePath), std::move(source), sourcePath);
}

namespace {

// Tries a path as vector, then as raster. Callers shouldn't have to declare
// which a file is - GDAL already knows, and requiring a flag would make
// `view a.shp b.tif` impossible to express.
//
// The vector error is the one reported if both fail: for a path that is
// neither, "failed to open" from the vector side is the more useful message,
// and a genuine raster that fails to *warp* throws RasterError out of here
// rather than being swallowed.
void addPath(Map& map, const std::string& path, const std::string& displayCrs, jobs::ThreadPool* pool) {
    try {
        Dataset dataset = Dataset::open(path, displayCrs);
        if (pool != nullptr) {
            map.addDataset(std::move(dataset), path, *pool);
        } else {
            map.addDataset(std::move(dataset), path);
        }
        return;
    } catch (const DatasetOpenError&) {
        // Not openable as vector - fall through and try raster.
    }
    map.addRaster(std::make_shared<raster::RasterSource>(path, displayCrs), path);
}

}  // namespace

Map Map::open(const std::vector<std::string>& paths, const std::string& displayCrs) {
    Map map;
    map.displayCrs_ = displayCrs;
    for (const std::string& path : paths) {
        addPath(map, path, displayCrs, nullptr);
    }
    return map;
}

Map Map::open(const std::vector<std::string>& paths, const std::string& displayCrs, jobs::ThreadPool& pool) {
    Map map;
    map.displayCrs_ = displayCrs;
    for (const std::string& path : paths) {
        addPath(map, path, displayCrs, &pool);
    }
    return map;
}

Map Map::open(const std::vector<std::string>& paths) {
    return open(paths, Dataset::defaultDisplayCrs());
}

Map Map::open(const std::vector<std::string>& paths, jobs::ThreadPool& pool) {
    return open(paths, Dataset::defaultDisplayCrs(), pool);
}

Map Map::open(const std::string& path) { return open(std::vector<std::string>{path}); }

Map Map::open(const std::string& path, const std::string& displayCrs) {
    return open(std::vector<std::string>{path}, displayCrs);
}

void Map::setDisplayCrs(const std::string& displayCrs) {
    if (displayCrs == displayCrs_) {
        return;
    }
    // Built fully before anything is swapped in, so a bad CRS or an
    // unreadable file throws with this map still intact.
    Map reloaded = open(sourcePaths_, displayCrs);
    restoreDisplayState(layers_, reloaded.layers_);
    *this = std::move(reloaded);
}

void Map::setDisplayCrs(const std::string& displayCrs, jobs::ThreadPool& pool) {
    if (displayCrs == displayCrs_) {
        return;
    }
    Map reloaded = open(sourcePaths_, displayCrs, pool);
    restoreDisplayState(layers_, reloaded.layers_);
    *this = std::move(reloaded);
}

Envelope Map::extent() const {
    Envelope extent;
    for (const MapLayer& mapLayer : layers_) {
        extent.expand(mapLayer.extent());
    }
    return extent;
}

std::size_t Map::featureCount() const {
    std::size_t count = 0;
    for (const MapLayer& mapLayer : layers_) {
        if (!mapLayer.isRaster()) {
            count += mapLayer.layer().features().size();
        }
    }
    return count;
}

}  // namespace cartograph
