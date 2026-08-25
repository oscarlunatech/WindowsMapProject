#include "cartograph/query/identify.h"

#include <algorithm>
#include <cstddef>

#include "cartograph/geom/predicates.h"

namespace cartograph::query {

std::vector<Hit> identify(const Dataset& dataset, const std::vector<render::LayerCache>& layerCaches,
                           Point2D mapPoint, double tolerance) {
    const std::vector<Layer>& layers = dataset.layers();
    const std::size_t layerCount = (std::min)(layers.size(), layerCaches.size());

    // The R-tree stores feature extents, so widen the probe point into a
    // tolerance-sized box before querying: a feature can be within tolerance
    // of the point while its extent doesn't contain the point itself.
    Envelope probe;
    probe.expand(Point2D{mapPoint.x - tolerance, mapPoint.y - tolerance});
    probe.expand(Point2D{mapPoint.x + tolerance, mapPoint.y + tolerance});

    std::vector<Hit> hits;
    for (std::size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
        const std::vector<Feature>& features = layers[layerIndex].features();

        for (const std::size_t featureIndex : layerCaches[layerIndex].query(probe)) {
            if (featureIndex >= features.size()) {
                continue;
            }
            // Exact test against the *original* geometry, not the simplified
            // geometry the renderer draws - identify should answer about the
            // real data, not about whatever the current zoom happened to
            // round it to.
            const double distance = geom::distanceTo(features[featureIndex].geometry(), mapPoint);
            if (distance <= tolerance) {
                hits.push_back(Hit{layerIndex, featureIndex, distance});
            }
        }
    }

    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.layerIndex != b.layerIndex) {
            return a.layerIndex > b.layerIndex;  // topmost layer first
        }
        if (a.distance != b.distance) {
            return a.distance < b.distance;  // then nearest first
        }
        return a.featureIndex < b.featureIndex;  // stable, deterministic tiebreak
    });
    return hits;
}

}  // namespace cartograph::query
