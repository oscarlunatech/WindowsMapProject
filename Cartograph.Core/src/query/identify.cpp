#include "cartograph/query/identify.h"

#include <algorithm>
#include <cstddef>

#include "cartograph/geom/predicates.h"

namespace cartograph::query {

std::vector<Hit> identify(const Map& map, Point2D mapPoint, double tolerance) {
    // The cache stores feature extents, so widen the probe point into a
    // tolerance-sized box before querying: a feature can be within tolerance
    // of the point while its extent doesn't contain the point itself.
    Envelope probe;
    probe.expand(Point2D{mapPoint.x - tolerance, mapPoint.y - tolerance});
    probe.expand(Point2D{mapPoint.x + tolerance, mapPoint.y + tolerance});

    std::vector<Hit> hits;
    for (std::size_t layerIndex = 0; layerIndex < map.layers().size(); ++layerIndex) {
        const MapLayer& mapLayer = map.layers()[layerIndex];
        if (!mapLayer.visible()) {
            continue;
        }

        if (mapLayer.isRaster()) {
            // A raster answers with the pixel under the point, not with a
            // nearest-feature search - tolerance has no meaning here, since
            // every point inside the raster is on it.
            std::vector<double> values = mapLayer.rasterSource()->sample(mapPoint);
            if (!values.empty()) {
                Hit hit;
                hit.layerIndex = layerIndex;
                hit.bandValues = std::move(values);
                hits.push_back(std::move(hit));
            }
            continue;
        }

        const std::vector<Feature>& features = mapLayer.layer().features();

        for (const std::size_t featureIndex : mapLayer.cache().query(probe)) {
            if (featureIndex >= features.size()) {
                continue;
            }
            const double distance = geom::distanceTo(features[featureIndex].geometry(), mapPoint);
            if (distance <= tolerance) {
                Hit hit;
                hit.layerIndex = layerIndex;
                hit.featureIndex = featureIndex;
                hit.distance = distance;
                hits.push_back(std::move(hit));
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
