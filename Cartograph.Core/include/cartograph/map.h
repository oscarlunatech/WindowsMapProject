#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cartograph/dataset.h"
#include "cartograph/jobs/thread_pool.h"
#include "cartograph/layer.h"
#include "cartograph/render/layer_cache.h"

namespace cartograph {

// One layer as it appears in a map: the data itself, the cache derived from
// it, and the per-map display state.
//
// The LayerCache lives here rather than in a vector running alongside, because
// it is *derived from this layer's geometry* - the two are invalidated by the
// same events, which is exactly what Phase 16 (mutable data) will need. The
// styling deliberately does not live here: a style::Stylesheet binds to the
// whole Map, so restyling never touches an R-tree (see the Phase 7 DECISIONS
// entry on keeping those two apart).
class MapLayer {
public:
    MapLayer(Layer layer, render::LayerCache cache, std::string sourcePath)
        : layer_(std::move(layer)), cache_(std::move(cache)), sourcePath_(std::move(sourcePath)) {}

    const Layer& layer() const { return layer_; }
    const render::LayerCache& cache() const { return cache_; }

    // The file this layer was read from. Several layers can share one path -
    // a single dataset often contains many.
    const std::string& sourcePath() const { return sourcePath_; }

    // Hidden layers are skipped by drawing *and* by identify: clicking what
    // you can't see shouldn't report a hit.
    bool visible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }

    // 0 (transparent) to 1 (opaque); values outside that range are clamped.
    float opacity() const { return opacity_; }
    void setOpacity(float opacity);

private:
    Layer layer_;
    render::LayerCache cache_;
    std::string sourcePath_;
    bool visible_ = true;
    float opacity_ = 1.0f;
};

// An ordered stack of layers gathered from one or more files, drawn
// bottom-to-top: layers()[0] is drawn first and ends up underneath, the last
// entry ends up on top.
//
// This replaces "one Dataset per run". Dataset is now purely the loader - it
// still owns the GDAL boundary and the reprojection - while a Map is what
// actually gets drawn, styled, and identified against.
class Map {
public:
    Map() = default;

    // Opens each path via Dataset::open and appends its layers in order, so
    // the resulting stack matches the order the paths were given in. Throws
    // DatasetOpenError if any path fails.
    //
    // The pool overload builds the (expensive) per-layer caches concurrently;
    // the serial one is for callers that don't have a pool handy, such as
    // tests. Neither may be called from inside a task already running on the
    // pool it's passed - see the no-nested-submission rule in thread_pool.h.
    static Map open(const std::vector<std::string>& paths);
    static Map open(const std::vector<std::string>& paths, jobs::ThreadPool& pool);
    static Map open(const std::string& path);

    void addDataset(Dataset dataset, const std::string& sourcePath);
    void addDataset(Dataset dataset, const std::string& sourcePath, jobs::ThreadPool& pool);

    const std::vector<MapLayer>& layers() const { return layers_; }
    std::vector<MapLayer>& layers() { return layers_; }

    bool empty() const { return layers_.empty(); }

    // Union of *every* layer's extent, hidden ones included - this is what a
    // "zoom to full extent" should show, and hiding a layer shouldn't silently
    // move the camera.
    Envelope extent() const;

    // Total across every layer, hidden ones included.
    std::size_t featureCount() const;

private:
    std::vector<MapLayer> layers_;
};

}  // namespace cartograph
