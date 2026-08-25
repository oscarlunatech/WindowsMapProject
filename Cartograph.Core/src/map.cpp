#include "cartograph/map.h"

#include <utility>

namespace cartograph {

void MapLayer::setOpacity(float opacity) {
    opacity_ = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
}

void Map::addDataset(Dataset dataset, const std::string& sourcePath) {
    std::vector<Layer> layers = dataset.takeLayers();
    layers_.reserve(layers_.size() + layers.size());
    for (Layer& layer : layers) {
        render::LayerCache cache(layer);
        layers_.emplace_back(std::move(layer), std::move(cache), sourcePath);
    }
}

void Map::addDataset(Dataset dataset, const std::string& sourcePath, jobs::ThreadPool& pool) {
    std::vector<Layer> layers = dataset.takeLayers();

    // buildLayerCachesParallel holds const references into `layers` while it
    // works, so nothing may move out of that vector until it returns.
    std::vector<render::LayerCache> caches = render::buildLayerCachesParallel(pool, layers);

    layers_.reserve(layers_.size() + layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        layers_.emplace_back(std::move(layers[i]), std::move(caches[i]), sourcePath);
    }
}

Map Map::open(const std::vector<std::string>& paths) {
    Map map;
    for (const std::string& path : paths) {
        map.addDataset(Dataset::open(path), path);
    }
    return map;
}

Map Map::open(const std::vector<std::string>& paths, jobs::ThreadPool& pool) {
    Map map;
    for (const std::string& path : paths) {
        map.addDataset(Dataset::open(path), path, pool);
    }
    return map;
}

Map Map::open(const std::string& path) { return open(std::vector<std::string>{path}); }

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
