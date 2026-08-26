#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "cartograph/dataset.h"
#include "cartograph/jobs/thread_pool.h"
#include "cartograph/layer.h"
#include "cartograph/raster/raster_source.h"
#include "cartograph/render/layer_cache.h"

namespace cartograph {

// One layer as it appears in a map: its content, and the per-map display
// state that applies whatever the content is.
//
// A layer is either **vector** (a Layer plus the LayerCache derived from it)
// or **raster** (a RasterSource plus the most recently read image). They share
// one ordered list rather than living in two, because draw order has to
// interleave - a raster basemap goes under the vectors, a raster overlay goes
// over them, and a parallel list can express neither.
//
// For vector layers the LayerCache lives here because it's derived from that
// layer's geometry and the two are invalidated by the same events - which is
// what Phase 16 will need. Styling deliberately does not live here: a
// style::Stylesheet binds to the whole Map, so restyling never touches an
// R-tree (see the Phase 7 DECISIONS entry).
class MapLayer {
public:
    // Vector.
    MapLayer(Layer layer, render::LayerCache cache, std::string sourcePath);
    // Raster. The source is shared rather than owned outright so a background
    // read can keep it alive independently of the Map being reprojected or
    // rebuilt underneath it.
    MapLayer(std::string name, std::shared_ptr<raster::RasterSource> source, std::string sourcePath);

    bool isRaster() const { return std::holds_alternative<RasterContent>(content_); }

    // Works for both kinds - the layer name for vector, the file's stem for
    // raster.
    const std::string& name() const;

    // Extent in the map's display CRS, whichever kind this is.
    Envelope extent() const;

    // Vector accessors. Calling these on a raster layer is a programming
    // error; check isRaster() first.
    const Layer& layer() const;
    const render::LayerCache& cache() const;

    // Raster accessors, same rule in reverse.
    const std::shared_ptr<raster::RasterSource>& rasterSource() const;

    // The most recently read window for a raster layer, or an empty image if
    // nothing has been read yet. The renderer draws whatever is here, scaled
    // from the image's own extent - so a stale image still lands in the right
    // place, just softer, until a fresher read replaces it.
    const raster::RasterImage& image() const;
    void setImage(raster::RasterImage image);

    // The file this layer was read from. Several layers can share one path -
    // a single vector dataset often contains many.
    const std::string& sourcePath() const { return sourcePath_; }

    // Hidden layers are skipped by drawing *and* by identify: clicking what
    // you can't see shouldn't report a hit.
    bool visible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }

    // 0 (transparent) to 1 (opaque); values outside that range are clamped.
    float opacity() const { return opacity_; }
    void setOpacity(float opacity);

private:
    struct VectorContent {
        Layer layer;
        render::LayerCache cache;
    };
    struct RasterContent {
        std::string name;
        std::shared_ptr<raster::RasterSource> source;
        raster::RasterImage image;
    };

    std::variant<VectorContent, RasterContent> content_;
    std::string sourcePath_;
    bool visible_ = true;
    float opacity_ = 1.0f;
};

// An ordered stack of layers gathered from one or more files, drawn
// bottom-to-top: layers()[0] is drawn first and ends up underneath, the last
// entry ends up on top.
//
// Dataset is the vector loader and raster::RasterSource the raster one; a Map
// is what actually gets drawn, styled, and identified against.
class Map {
public:
    Map() = default;

    // Opens each path and appends its layers in order, so the resulting stack
    // matches the order the paths were given in. Each path is tried as vector
    // first and as raster if that fails, so callers don't have to say which is
    // which. Every layer is reprojected to displayCrs as it loads. Throws
    // DatasetOpenError if a path can't be read either way.
    //
    // The pool overload builds the (expensive) per-layer vector caches
    // concurrently; the serial one is for callers without a pool handy, such
    // as tests. Neither may be called from inside a task already running on
    // the pool it's passed - see the no-nested-submission rule in
    // thread_pool.h.
    static Map open(const std::vector<std::string>& paths, const std::string& displayCrs);
    static Map open(const std::vector<std::string>& paths, const std::string& displayCrs,
                     jobs::ThreadPool& pool);
    static Map open(const std::vector<std::string>& paths);
    static Map open(const std::vector<std::string>& paths, jobs::ThreadPool& pool);
    static Map open(const std::string& path);
    static Map open(const std::string& path, const std::string& displayCrs);

    // The CRS every layer's coordinates are in. Empty only for a default-
    // constructed Map that has never had anything added.
    const std::string& displayCrs() const { return displayCrs_; }

    // Reprojects the whole map by **re-reading every source file** in the new
    // CRS and rebuilding each layer, then swapping the result in.
    //
    // Re-reading rather than transforming what's in memory is deliberate.
    // Vector layers are stored already-projected, so a second transform would
    // compose new <- current <- source and accumulate error across repeated
    // switches; keeping a pristine source copy would instead double the memory
    // of a 500k-feature map. Rasters can't be re-transformed in place at all -
    // reprojection resamples. Re-reading costs I/O on a rare, user-initiated
    // action and always produces exactly what opening in that CRS would have.
    //
    // Strongly exception-safe: on failure the map is left untouched. Per-layer
    // visibility and opacity are preserved; anything holding a MapLayer
    // reference or a style::Stylesheet built against this map must be rebuilt,
    // since the layers are replaced wholesale.
    void setDisplayCrs(const std::string& displayCrs);
    void setDisplayCrs(const std::string& displayCrs, jobs::ThreadPool& pool);

    void addDataset(Dataset dataset, const std::string& sourcePath);
    void addDataset(Dataset dataset, const std::string& sourcePath, jobs::ThreadPool& pool);
    void addRaster(std::shared_ptr<raster::RasterSource> source, const std::string& sourcePath);

    const std::vector<MapLayer>& layers() const { return layers_; }
    std::vector<MapLayer>& layers() { return layers_; }

    bool empty() const { return layers_.empty(); }

    // Union of *every* layer's extent, hidden ones included - this is what a
    // "zoom to full extent" should show, and hiding a layer shouldn't silently
    // move the camera.
    Envelope extent() const;

    // Total vector features across every layer, hidden ones included. Raster
    // layers contribute nothing - they have no features to count.
    std::size_t featureCount() const;

private:
    std::vector<MapLayer> layers_;
    // The paths given to open()/add*(), in order, duplicates kept.
    // Deliberately *not* derived from layers_' source paths on demand:
    // loading the same file twice on purpose (to style or filter it two ways)
    // is legitimate, and de-duplicating would silently drop a layer on the
    // next setDisplayCrs.
    std::vector<std::string> sourcePaths_;
    std::string displayCrs_;
};

}  // namespace cartograph
