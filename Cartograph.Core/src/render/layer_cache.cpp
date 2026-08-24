#include "cartograph/render/layer_cache.h"

#include <cmath>
#include <future>

#include "cartograph/geom/simplify.h"

namespace cartograph::render {

namespace {

constexpr int kBucketCount = 6;
constexpr double kSimplifyPixelThreshold = 1.0;  // simplify away detail smaller than ~1 screen pixel

std::vector<double> computeToleranceBuckets(const Envelope& extent) {
    const double diagonal = std::hypot(extent.width(), extent.height());
    const double maxTolerance = diagonal * 0.001;  // coarsest bucket: 0.1% of the layer's own diagonal

    std::vector<double> tolerances(kBucketCount);
    tolerances[0] = 0.0;
    for (int i = 1; i < kBucketCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kBucketCount - 1);
        tolerances[static_cast<std::size_t>(i)] = maxTolerance * t * t;  // quadratic ramp favors finer buckets
    }
    return tolerances;
}

}  // namespace

LayerCache::LayerCache(const Layer& layer)
    : spatialIndex_(layer), tolerances_(computeToleranceBuckets(layer.extent())) {
    simplifiedByBucket_.resize(tolerances_.size());
    for (std::size_t bucket = 0; bucket < tolerances_.size(); ++bucket) {
        simplifiedByBucket_[bucket].reserve(layer.features().size());
        for (const Feature& feature : layer.features()) {
            simplifiedByBucket_[bucket].push_back(geom::simplify(feature.geometry(), tolerances_[bucket]));
        }
    }
}

std::vector<std::size_t> LayerCache::query(const Envelope& viewExtent) const {
    return spatialIndex_.query(viewExtent);
}

const Geometry& LayerCache::simplifiedGeometry(std::size_t featureIndex, double mapUnitsPerPixel) const {
    const double targetTolerance = mapUnitsPerPixel * kSimplifyPixelThreshold;

    std::size_t bucket = 0;
    for (std::size_t i = 0; i < tolerances_.size(); ++i) {
        if (tolerances_[i] <= targetTolerance) {
            bucket = i;
        } else {
            break;  // tolerances_ is ascending
        }
    }

    return simplifiedByBucket_[bucket][featureIndex];
}

std::vector<LayerCache> buildLayerCachesParallel(jobs::ThreadPool& pool, const std::vector<Layer>& layers) {
    std::vector<std::future<LayerCache>> futures;
    futures.reserve(layers.size());
    for (const Layer& layer : layers) {
        futures.push_back(pool.submit([&layer] { return LayerCache(layer); }));
    }

    std::vector<LayerCache> caches;
    caches.reserve(futures.size());
    for (std::future<LayerCache>& future : futures) {
        caches.push_back(future.get());
    }
    return caches;
}

}  // namespace cartograph::render
