#include "cartograph/render/renderer.h"

#include <d2d1.h>
#include <d2d1helper.h>
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <future>

using Microsoft::WRL::ComPtr;

namespace cartograph::render {

namespace {

void ensureComInitialized() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;
    // Ignore "already initialized" outcomes (S_FALSE / RPC_E_CHANGED_MODE) -
    // only a genuine failure is fatal.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}

void throwIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        throw RenderError(std::string(what) + " failed (hr=" + std::to_string(hr) + ")");
    }
}

std::wstring widen(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), size);
    return result;
}

ComPtr<IWICImagingFactory> createWicFactory() {
    ensureComInitialized();
    ComPtr<IWICImagingFactory> factory;
    throwIfFailed(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
        "CoCreateInstance(WICImagingFactory)");
    return factory;
}

D2D1_POINT_2F toD2DPoint(Point2D p) {
    return D2D1::Point2F(static_cast<float>(p.x), static_cast<float>(p.y));
}

// One figure per ring per geometry; filled+closed for polygons, hollow+open
// for lines. Fill mode is alternate so polygon holes render correctly
// regardless of ring winding order. Takes multiple geometries so callers can
// batch many features into one ID2D1PathGeometry and one draw call.
ComPtr<ID2D1PathGeometry> buildPathGeometry(ID2D1Factory& factory, const Viewport& viewport,
                                             const std::vector<const Geometry*>& geometries, bool filled) {
    ComPtr<ID2D1PathGeometry> path;
    throwIfFailed(factory.CreatePathGeometry(&path), "CreatePathGeometry");

    ComPtr<ID2D1GeometrySink> sink;
    throwIfFailed(path->Open(&sink), "ID2D1PathGeometry::Open");
    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);

    const D2D1_FIGURE_BEGIN beginMode = filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW;
    const D2D1_FIGURE_END endMode = filled ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN;

    for (const Geometry* geometry : geometries) {
        for (const Part& part : geometry->parts()) {
            for (const Ring& ring : part) {
                if (ring.empty()) {
                    continue;
                }
                sink->BeginFigure(toD2DPoint(viewport.mapToScreen(ring.front())), beginMode);
                for (std::size_t i = 1; i < ring.size(); ++i) {
                    sink->AddLine(toD2DPoint(viewport.mapToScreen(ring[i])));
                }
                sink->EndFigure(endMode);
            }
        }
    }

    throwIfFailed(sink->Close(), "ID2D1GeometrySink::Close");
    return path;
}

ComPtr<ID2D1PathGeometry> buildPathGeometry(ID2D1Factory& factory, const Viewport& viewport,
                                             const Geometry& geometry, bool filled) {
    return buildPathGeometry(factory, viewport, std::vector<const Geometry*>{&geometry}, filled);
}

// A ring already transformed to screen space - plain data, safe to compute
// on a worker thread since it touches no D2D objects.
using ScreenRing = std::vector<D2D1_POINT_2F>;

std::vector<ScreenRing> transformGeometryRings(const Viewport& viewport,
                                                const std::vector<const Geometry*>& geometries) {
    std::vector<ScreenRing> screenRings;
    for (const Geometry* geometry : geometries) {
        for (const Part& part : geometry->parts()) {
            for (const Ring& ring : part) {
                if (ring.empty()) {
                    continue;
                }
                ScreenRing screenRing;
                screenRing.reserve(ring.size());
                for (const Point2D& p : ring) {
                    screenRing.push_back(toD2DPoint(viewport.mapToScreen(p)));
                }
                screenRings.push_back(std::move(screenRing));
            }
        }
    }
    return screenRings;
}

// Same sink-writing shape as buildPathGeometry above, but consuming
// already-transformed screen-space rings instead of Geometry+Viewport - the
// D2D-touching half of what buildPathGeometry used to do in one pass.
ComPtr<ID2D1PathGeometry> buildPathGeometryFromRings(ID2D1Factory& factory, const std::vector<ScreenRing>& rings,
                                                      bool filled) {
    ComPtr<ID2D1PathGeometry> path;
    throwIfFailed(factory.CreatePathGeometry(&path), "CreatePathGeometry");

    ComPtr<ID2D1GeometrySink> sink;
    throwIfFailed(path->Open(&sink), "ID2D1PathGeometry::Open");
    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);

    const D2D1_FIGURE_BEGIN beginMode = filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW;
    const D2D1_FIGURE_END endMode = filled ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN;

    for (const ScreenRing& ring : rings) {
        sink->BeginFigure(ring.front(), beginMode);
        for (std::size_t i = 1; i < ring.size(); ++i) {
            sink->AddLine(ring[i]);
        }
        sink->EndFigure(endMode);
    }

    throwIfFailed(sink->Close(), "ID2D1GeometrySink::Close");
    return path;
}

// One symbol's worth of already-batched drawing work. lineGeometry/
// polygonGeometry are fully-built ID2D1PathGeometry resources - safe to
// build on a pool worker (not just the coordinate math) because the factory
// is created D2D1_FACTORY_TYPE_MULTI_THREADED, which allows concurrent
// resource creation as long as no two threads touch the *same* resource at
// once (each worker builds its own layers' geometries, never shared). Only
// ID2D1RenderTarget itself stays thread-affine regardless of factory type,
// which is why the actual DrawGeometry/FillGeometry/FillEllipse calls in
// drawDatasetCulled below stay on the caller's thread.
struct PreparedBatch {
    std::size_t symbolIndex = 0;
    ComPtr<ID2D1PathGeometry> lineGeometry;     // null if this batch has no visible lines
    ComPtr<ID2D1PathGeometry> polygonGeometry;  // null if this batch has no visible polygons
    std::vector<D2D1_POINT_2F> points;
};

struct PreparedLayer {
    std::vector<PreparedBatch> batches;  // ascending by symbolIndex, empty groups dropped
    std::size_t visibleCount = 0;
};

PreparedLayer prepareLayer(ID2D1Factory& factory, const LayerCache& cache, const Viewport& viewport,
                            double mapUnitsPerPixel, const Envelope& viewExtent,
                            const style::Stylesheet& stylesheet, std::size_t layerIndex) {
    PreparedLayer prepared;
    const std::vector<std::size_t> visible = cache.query(viewExtent);
    prepared.visibleCount = visible.size();
    if (visible.empty()) {
        return prepared;
    }

    // Bucket this layer's visible features by the symbol they resolve to, so
    // each distinct symbol still ends up as one batched geometry per geometry
    // class. Indexed by symbol rather than keyed in a map: symbol tables are
    // tiny (Stylesheet dedupes by value), so a flat vector walked in index
    // order is both cheaper and deterministic in draw order.
    struct Group {
        std::vector<const Geometry*> lines;
        std::vector<const Geometry*> polygons;
        std::vector<D2D1_POINT_2F> points;
    };
    std::vector<Group> groups(stylesheet.symbolCount());

    for (std::size_t featureIdx : visible) {
        Group& group = groups[stylesheet.symbolIndex(layerIndex, featureIdx)];
        const Geometry& geometry = cache.simplifiedGeometry(featureIdx, mapUnitsPerPixel);
        switch (geometry.type()) {
            case GeometryType::Point:
            case GeometryType::MultiPoint:
                for (const Part& part : geometry.parts()) {
                    for (const Ring& ring : part) {
                        for (const Point2D& p : ring) {
                            group.points.push_back(toD2DPoint(viewport.mapToScreen(p)));
                        }
                    }
                }
                break;
            case GeometryType::LineString:
            case GeometryType::MultiLineString:
                group.lines.push_back(&geometry);
                break;
            case GeometryType::Polygon:
            case GeometryType::MultiPolygon:
                group.polygons.push_back(&geometry);
                break;
            case GeometryType::Unknown:
                break;
        }
    }

    for (std::size_t symbolIdx = 0; symbolIdx < groups.size(); ++symbolIdx) {
        Group& group = groups[symbolIdx];
        if (group.lines.empty() && group.polygons.empty() && group.points.empty()) {
            continue;
        }

        PreparedBatch batch;
        batch.symbolIndex = symbolIdx;
        batch.points = std::move(group.points);
        if (!group.lines.empty()) {
            const std::vector<ScreenRing> lineRings = transformGeometryRings(viewport, group.lines);
            batch.lineGeometry = buildPathGeometryFromRings(factory, lineRings, /*filled=*/false);
        }
        if (!group.polygons.empty()) {
            const std::vector<ScreenRing> polygonRings = transformGeometryRings(viewport, group.polygons);
            batch.polygonGeometry = buildPathGeometryFromRings(factory, polygonRings, /*filled=*/true);
        }
        prepared.batches.push_back(std::move(batch));
    }
    return prepared;
}

// The four brushes one style::Symbol needs. Brushes are render-target
// resources, so unlike the geometry above these can only be created on the
// caller's thread - done once per frame for the whole (deduplicated, hence
// small) symbol table rather than per batch, so a categorized layer doesn't
// re-create the same brush once per category per frame.
struct SymbolBrushes {
    ComPtr<ID2D1SolidColorBrush> fill;
    ComPtr<ID2D1SolidColorBrush> polygonStroke;
    ComPtr<ID2D1SolidColorBrush> lineStroke;
    ComPtr<ID2D1SolidColorBrush> pointFill;
};

D2D1_COLOR_F toD2DColor(const style::Color& color) {
    return D2D1::ColorF(color.r, color.g, color.b, color.a);
}

std::vector<SymbolBrushes> createSymbolBrushes(ID2D1RenderTarget& target, const style::Stylesheet& stylesheet) {
    std::vector<SymbolBrushes> brushes;
    brushes.reserve(stylesheet.symbolCount());
    for (const style::Symbol& symbol : stylesheet.symbols()) {
        SymbolBrushes symbolBrushes;
        throwIfFailed(target.CreateSolidColorBrush(toD2DColor(symbol.fill), &symbolBrushes.fill),
                      "CreateSolidColorBrush(fill)");
        throwIfFailed(
            target.CreateSolidColorBrush(toD2DColor(symbol.polygonStroke), &symbolBrushes.polygonStroke),
            "CreateSolidColorBrush(polygonStroke)");
        throwIfFailed(target.CreateSolidColorBrush(toD2DColor(symbol.lineStroke), &symbolBrushes.lineStroke),
                      "CreateSolidColorBrush(lineStroke)");
        throwIfFailed(target.CreateSolidColorBrush(toD2DColor(symbol.pointFill), &symbolBrushes.pointFill),
                      "CreateSolidColorBrush(pointFill)");
        brushes.push_back(std::move(symbolBrushes));
    }
    return brushes;
}

// A stylesheet built against a different map would index out of bounds in the
// hot loop; catch the mix-up at the call boundary instead.
void requireStylesheetMatches(const Map& map, const style::Stylesheet& stylesheet) {
    if (stylesheet.layerCount() != map.layers().size()) {
        throw RenderError("stylesheet was built against a different map (" +
                          std::to_string(stylesheet.layerCount()) + " layers vs " +
                          std::to_string(map.layers().size()) + ")");
    }
}

// Layer opacity is applied by setting it on the brushes for the duration of
// that layer, rather than by pushing an ID2D1Layer. PushLayer would give true
// group opacity (overlapping features within one layer compositing as a unit)
// at the cost of an intermediate surface per layer, which on a 21-layer map is
// a lot to pay every frame. Setting brush opacity composites each feature
// separately instead - visibly different only where features inside the same
// layer overlap each other, which for the layer stacks this viewer targets is
// a good trade. Revisit if a use case needs the exact semantics.
// Uploads a RasterImage to the render target and draws it into the rectangle
// its own extent maps to.
//
// Placed by the image's extent rather than the viewport's, which is what makes
// a stale image behave sensibly: after a pan the previous read still lands on
// the ground it actually covers, merely offset and soft, until a fresher read
// replaces it - rather than sliding around with the camera.
void drawRasterImage(ID2D1RenderTarget& target, const raster::RasterImage& image, const Viewport& viewport,
                      float opacity) {
    if (image.empty() || !image.extent.valid || opacity <= 0.0f) {
        return;
    }

    ComPtr<ID2D1Bitmap> bitmap;
    const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    const HRESULT hr = target.CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(image.width), static_cast<UINT32>(image.height)), image.bgra.data(),
        static_cast<UINT32>(image.width) * 4, properties, &bitmap);
    if (FAILED(hr)) {
        throw RenderError("CreateBitmap (raster) failed (hr=" + std::to_string(hr) + ")");
    }

    // Top-left and bottom-right of the image's extent in screen space. mapToScreen
    // flips Y, so the extent's maxY is the destination rect's top.
    const Point2D topLeft = viewport.mapToScreen(Point2D{image.extent.minX, image.extent.maxY});
    const Point2D bottomRight = viewport.mapToScreen(Point2D{image.extent.maxX, image.extent.minY});
    const D2D1_RECT_F destination =
        D2D1::RectF(static_cast<float>(topLeft.x), static_cast<float>(topLeft.y),
                     static_cast<float>(bottomRight.x), static_cast<float>(bottomRight.y));

    target.DrawBitmap(bitmap.Get(), destination, opacity,
                       D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void setBrushOpacity(const std::vector<SymbolBrushes>& brushes, float opacity) {
    for (const SymbolBrushes& symbolBrushes : brushes) {
        symbolBrushes.fill->SetOpacity(opacity);
        symbolBrushes.polygonStroke->SetOpacity(opacity);
        symbolBrushes.lineStroke->SetOpacity(opacity);
        symbolBrushes.pointFill->SetOpacity(opacity);
    }
}

void drawGeometry(ID2D1Factory& factory, ID2D1RenderTarget& target, const Viewport& viewport,
                   const style::Symbol& symbol, const SymbolBrushes& brushes, const Geometry& geometry) {
    switch (geometry.type()) {
        case GeometryType::Point:
        case GeometryType::MultiPoint: {
            if (symbol.pointRadius <= 0.0f) {
                break;
            }
            for (const Part& part : geometry.parts()) {
                for (const Ring& ring : part) {
                    for (const Point2D& p : ring) {
                        const D2D1_POINT_2F center = toD2DPoint(viewport.mapToScreen(p));
                        target.FillEllipse(D2D1::Ellipse(center, symbol.pointRadius, symbol.pointRadius),
                                            brushes.pointFill.Get());
                    }
                }
            }
            break;
        }
        case GeometryType::LineString:
        case GeometryType::MultiLineString: {
            if (symbol.lineStrokeWidth <= 0.0f) {
                break;
            }
            const auto path = buildPathGeometry(factory, viewport, geometry, /*filled=*/false);
            target.DrawGeometry(path.Get(), brushes.lineStroke.Get(), symbol.lineStrokeWidth);
            break;
        }
        case GeometryType::Polygon:
        case GeometryType::MultiPolygon: {
            const auto path = buildPathGeometry(factory, viewport, geometry, /*filled=*/true);
            target.FillGeometry(path.Get(), brushes.fill.Get());
            if (symbol.polygonStrokeWidth > 0.0f) {
                target.DrawGeometry(path.Get(), brushes.polygonStroke.Get(), symbol.polygonStrokeWidth);
            }
            break;
        }
        case GeometryType::Unknown:
            break;
    }
}

}  // namespace

void refreshRasterLayers(Map& map, const Viewport& viewport, const style::Stylesheet& stylesheet) {
    requireStylesheetMatches(map, stylesheet);
    const Envelope window = viewport.mapExtent();

    for (std::size_t layerIdx = 0; layerIdx < map.layers().size(); ++layerIdx) {
        MapLayer& mapLayer = map.layers()[layerIdx];
        if (!mapLayer.isRaster() || !mapLayer.visible()) {
            continue;
        }
        mapLayer.setImage(mapLayer.rasterSource()->read(window, viewport.screenSize().width,
                                                         viewport.screenSize().height,
                                                         stylesheet.rasterStyle(layerIdx)));
    }
}

void drawMap(ID2D1RenderTarget& target, ID2D1Factory& factory, const Map& map, const Viewport& viewport,
              const style::Stylesheet& stylesheet) {
    requireStylesheetMatches(map, stylesheet);
    const std::vector<SymbolBrushes> brushes = createSymbolBrushes(target, stylesheet);

    for (std::size_t layerIdx = 0; layerIdx < map.layers().size(); ++layerIdx) {
        const MapLayer& mapLayer = map.layers()[layerIdx];
        if (!mapLayer.visible()) {
            continue;
        }
        if (mapLayer.isRaster()) {
            drawRasterImage(target, mapLayer.image(), viewport, mapLayer.opacity());
            continue;
        }
        setBrushOpacity(brushes, mapLayer.opacity());

        const std::vector<Feature>& features = mapLayer.layer().features();
        for (std::size_t featureIdx = 0; featureIdx < features.size(); ++featureIdx) {
            const std::size_t symbolIdx = stylesheet.symbolIndex(layerIdx, featureIdx);
            drawGeometry(factory, target, viewport, stylesheet.symbol(symbolIdx), brushes[symbolIdx],
                          features[featureIdx].geometry());
        }
    }
}

void drawMap(ID2D1RenderTarget& target, ID2D1Factory& factory, const Map& map, const Viewport& viewport) {
    drawMap(target, factory, map, viewport, style::Stylesheet::defaults(map));
}

std::size_t drawMapCulled(ID2D1RenderTarget& target, ID2D1Factory& factory, const Map& map,
                           const Viewport& viewport, jobs::ThreadPool& pool,
                           const style::Stylesheet& stylesheet) {
    requireStylesheetMatches(map, stylesheet);
    const std::vector<SymbolBrushes> brushes = createSymbolBrushes(target, stylesheet);
    const double mapUnitsPerPixel = viewport.scale() > 0.0 ? 1.0 / viewport.scale() : 0.0;
    const Envelope viewExtent = viewport.mapExtent();

    const std::size_t layerCount = map.layers().size();

    // Phase 1 (parallel): query + simplify + transform + build each layer's
    // ID2D1PathGeometry resources. Safe on pool workers because factory is
    // D2D1_FACTORY_TYPE_MULTI_THREADED (see PreparedLayer's comment) - only
    // ID2D1RenderTarget itself needs to stay off worker threads. Chunked
    // into at most pool.size() tasks (each covering a contiguous range of
    // layers) rather than one task per layer: with ~20 layers, one-task-
    // per-layer means per-task overhead (a heap-allocated packaged_task, a
    // mutex lock, a condition-variable wake) dominates the actual work every
    // frame, which measured as a net slowdown. Each chunk writes only its
    // own disjoint slice of prepared, so this is race-free without needing
    // a lock. Submitted from this (non-worker) thread and collected below
    // before returning, so this never nests pool work inside pool work.
    std::vector<PreparedLayer> prepared(layerCount);
    const std::size_t chunkCount = layerCount == 0 ? 0 : (std::min)(layerCount, pool.size());
    std::vector<std::future<void>> futures;
    futures.reserve(chunkCount);
    for (std::size_t chunk = 0; chunk < chunkCount; ++chunk) {
        const std::size_t begin = layerCount * chunk / chunkCount;
        const std::size_t end = layerCount * (chunk + 1) / chunkCount;
        futures.push_back(pool.submit([&factory, &map, &viewport, mapUnitsPerPixel, viewExtent, &prepared,
                                        &stylesheet, begin, end] {
            for (std::size_t layerIdx = begin; layerIdx < end; ++layerIdx) {
                // Hidden layers cost nothing: no query, no simplification, no
                // geometry built, and prepared[layerIdx] stays empty so phase
                // 2 below skips it too. Raster layers prepare nothing here
                // either - their pixels were read elsewhere (off this thread,
                // see RasterSource) and phase 2 just blits them.
                const MapLayer& mapLayer = map.layers()[layerIdx];
                if (!mapLayer.visible() || mapLayer.isRaster()) {
                    continue;
                }
                prepared[layerIdx] = prepareLayer(factory, map.layers()[layerIdx].cache(), viewport,
                                                   mapUnitsPerPixel, viewExtent, stylesheet, layerIdx);
            }
        }));
    }
    for (std::future<void>& future : futures) {
        future.get();
    }

    // Phase 2 (serial): submit each layer's already-built geometry to the
    // render target on this thread - ID2D1RenderTarget itself is thread-
    // affine regardless of factory type, so this is the only place allowed
    // to touch it. Cheap relative to phase 1 now that the sink-writing
    // (BeginFigure/AddLine per point) happened in parallel above.
    std::size_t drawnCount = 0;
    for (std::size_t layerIdx = 0; layerIdx < prepared.size(); ++layerIdx) {
        const MapLayer& mapLayer = map.layers()[layerIdx];
        if (mapLayer.isRaster()) {
            if (mapLayer.visible()) {
                drawRasterImage(target, mapLayer.image(), viewport, mapLayer.opacity());
            }
            continue;
        }

        const PreparedLayer& layer = prepared[layerIdx];
        drawnCount += layer.visibleCount;  // 0 for a hidden layer, which prepares nothing
        if (layer.batches.empty()) {
            continue;
        }
        setBrushOpacity(brushes, mapLayer.opacity());

        for (const PreparedBatch& batch : layer.batches) {
            const style::Symbol& symbol = stylesheet.symbol(batch.symbolIndex);
            const SymbolBrushes& symbolBrushes = brushes[batch.symbolIndex];

            if (symbol.pointRadius > 0.0f) {
                for (const D2D1_POINT_2F& center : batch.points) {
                    target.FillEllipse(D2D1::Ellipse(center, symbol.pointRadius, symbol.pointRadius),
                                        symbolBrushes.pointFill.Get());
                }
            }
            if (batch.lineGeometry && symbol.lineStrokeWidth > 0.0f) {
                target.DrawGeometry(batch.lineGeometry.Get(), symbolBrushes.lineStroke.Get(),
                                     symbol.lineStrokeWidth);
            }
            if (batch.polygonGeometry) {
                target.FillGeometry(batch.polygonGeometry.Get(), symbolBrushes.fill.Get());
                if (symbol.polygonStrokeWidth > 0.0f) {
                    target.DrawGeometry(batch.polygonGeometry.Get(), symbolBrushes.polygonStroke.Get(),
                                         symbol.polygonStrokeWidth);
                }
            }
        }
    }

    return drawnCount;
}

OffscreenTarget::OffscreenTarget(ScreenSize size) {
    const ComPtr<IWICImagingFactory> wicFactory = createWicFactory();

    // MULTI_THREADED: shared by Renderer::render (drawMap, single-threaded
    // regardless - unaffected) and bench --culled (drawMapCulled, which needs
    // it - see renderer.h).
    throwIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, d2dFactory_.GetAddressOf()),
                  "D2D1CreateFactory");

    throwIfFailed(wicFactory->CreateBitmap(static_cast<UINT>(size.width), static_cast<UINT>(size.height),
                                            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand,
                                            &wicBitmap_),
                  "IWICImagingFactory::CreateBitmap");

    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    throwIfFailed(d2dFactory_->CreateWicBitmapRenderTarget(wicBitmap_.Get(), props, &renderTarget_),
                  "CreateWicBitmapRenderTarget");
}

void OffscreenTarget::beginFrame() {
    renderTarget_->BeginDraw();
    renderTarget_->Clear(D2D1::ColorF(D2D1::ColorF::White));
}

void OffscreenTarget::endFrame() {
    throwIfFailed(renderTarget_->EndDraw(), "ID2D1RenderTarget::EndDraw");
}

ComPtr<IWICBitmap> Renderer::render(const Map& map, const Viewport& viewport,
                                     const style::Stylesheet& stylesheet) {
    OffscreenTarget target(viewport.screenSize());
    target.beginFrame();
    drawMap(target.renderTarget(), target.factory(), map, viewport, stylesheet);
    target.endFrame();
    return target.bitmap();
}

ComPtr<IWICBitmap> Renderer::render(const Map& map, const Viewport& viewport) {
    return render(map, viewport, style::Stylesheet::defaults(map));
}

void savePng(IWICBitmap* bitmap, const std::string& path) {
    const ComPtr<IWICImagingFactory> wicFactory = createWicFactory();

    ComPtr<IWICStream> stream;
    throwIfFailed(wicFactory->CreateStream(&stream), "IWICImagingFactory::CreateStream");
    throwIfFailed(stream->InitializeFromFilename(widen(path).c_str(), GENERIC_WRITE),
                  "IWICStream::InitializeFromFilename");

    ComPtr<IWICBitmapEncoder> encoder;
    throwIfFailed(wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder),
                  "IWICImagingFactory::CreateEncoder");
    throwIfFailed(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "IWICBitmapEncoder::Initialize");

    ComPtr<IWICBitmapFrameEncode> frame;
    throwIfFailed(encoder->CreateNewFrame(&frame, nullptr), "IWICBitmapEncoder::CreateNewFrame");
    throwIfFailed(frame->Initialize(nullptr), "IWICBitmapFrameEncode::Initialize");

    UINT width = 0;
    UINT height = 0;
    throwIfFailed(bitmap->GetSize(&width, &height), "IWICBitmap::GetSize");
    throwIfFailed(frame->SetSize(width, height), "IWICBitmapFrameEncode::SetSize");

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    throwIfFailed(frame->SetPixelFormat(&format), "IWICBitmapFrameEncode::SetPixelFormat");

    throwIfFailed(frame->WriteSource(bitmap, nullptr), "IWICBitmapFrameEncode::WriteSource");
    throwIfFailed(frame->Commit(), "IWICBitmapFrameEncode::Commit");
    throwIfFailed(encoder->Commit(), "IWICBitmapEncoder::Commit");
}

}  // namespace cartograph::render
