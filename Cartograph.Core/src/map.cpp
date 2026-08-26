#include "cartograph/map.h"

#include <algorithm>
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

}  // namespace

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

Map Map::open(const std::vector<std::string>& paths, const std::string& displayCrs) {
    Map map;
    map.displayCrs_ = displayCrs;
    for (const std::string& path : paths) {
        map.addDataset(Dataset::open(path, displayCrs), path);
    }
    return map;
}

Map Map::open(const std::vector<std::string>& paths, const std::string& displayCrs, jobs::ThreadPool& pool) {
    Map map;
    map.displayCrs_ = displayCrs;
    for (const std::string& path : paths) {
        map.addDataset(Dataset::open(path, displayCrs), path, pool);
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
        extent.expand(mapLayer.layer().extent());
    }
    return extent;
}

std::size_t Map::featureCount() const {
    std::size_t count = 0;
    for (const MapLayer& mapLayer : layers_) {
        count += mapLayer.layer().features().size();
    }
    return count;
}

}  // namespace cartograph
