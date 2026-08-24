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

// Everything drawDatasetCulled needs from one layer. lineGeometry/
// polygonGeometry are fully-built ID2D1PathGeometry resources - safe to
// build on a pool worker (not just the coordinate math) because the factory
// is created D2D1_FACTORY_TYPE_MULTI_THREADED, which allows concurrent
// resource creation as long as no two threads touch the *same* resource at
// once (each worker builds its own layers' geometries, never shared). Only
// ID2D1RenderTarget itself stays thread-affine regardless of factory type,
// which is why the actual DrawGeometry/FillGeometry/FillEllipse calls in
// drawDatasetCulled below stay on the caller's thread.
struct PreparedLayer {
    ComPtr<ID2D1PathGeometry> lineGeometry;     // null if the layer has no visible lines
    ComPtr<ID2D1PathGeometry> polygonGeometry;  // null if the layer has no visible polygons
    std::vector<D2D1_POINT_2F> points;
    std::size_t visibleCount = 0;
};

PreparedLayer prepareLayer(ID2D1Factory& factory, const LayerCache& cache, const Viewport& viewport,
                            double mapUnitsPerPixel, const Envelope& viewExtent) {
    PreparedLayer prepared;
    const std::vector<std::size_t> visible = cache.query(viewExtent);
    prepared.visibleCount = visible.size();

    std::vector<const Geometry*> lineGeoms;
    std::vector<const Geometry*> polygonGeoms;

    for (std::size_t featureIdx : visible) {
        const Geometry& geometry = cache.simplifiedGeometry(featureIdx, mapUnitsPerPixel);
        switch (geometry.type()) {
            case GeometryType::Point:
            case GeometryType::MultiPoint:
                for (const Part& part : geometry.parts()) {
                    for (const Ring& ring : part) {
                        for (const Point2D& p : ring) {
                            prepared.points.push_back(toD2DPoint(viewport.mapToScreen(p)));
                        }
                    }
                }
                break;
            case GeometryType::LineString:
            case GeometryType::MultiLineString:
                lineGeoms.push_back(&geometry);
                break;
            case GeometryType::Polygon:
            case GeometryType::MultiPolygon:
                polygonGeoms.push_back(&geometry);
                break;
            case GeometryType::Unknown:
                break;
        }
    }

    if (!lineGeoms.empty()) {
        const std::vector<ScreenRing> lineRings = transformGeometryRings(viewport, lineGeoms);
        prepared.lineGeometry = buildPathGeometryFromRings(factory, lineRings, /*filled=*/false);
    }
    if (!polygonGeoms.empty()) {
        const std::vector<ScreenRing> polygonRings = transformGeometryRings(viewport, polygonGeoms);
        prepared.polygonGeometry = buildPathGeometryFromRings(factory, polygonRings, /*filled=*/true);
    }
    return prepared;
}

struct Brushes {
    ComPtr<ID2D1SolidColorBrush> polygonFill;
    ComPtr<ID2D1SolidColorBrush> polygonStroke;
    ComPtr<ID2D1SolidColorBrush> lineStroke;
    ComPtr<ID2D1SolidColorBrush> pointFill;
};

// Hardcoded placeholder styling; Phase 7 replaces this with real symbology.
Brushes createBrushes(ID2D1RenderTarget& target) {
    Brushes brushes;
    throwIfFailed(target.CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.85f, 0.85f), &brushes.polygonFill),
                  "CreateSolidColorBrush(polygonFill)");
    throwIfFailed(target.CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brushes.polygonStroke),
                  "CreateSolidColorBrush(polygonStroke)");
    throwIfFailed(target.CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.2f, 0.8f), &brushes.lineStroke),
                  "CreateSolidColorBrush(lineStroke)");
    throwIfFailed(target.CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red), &brushes.pointFill),
                  "CreateSolidColorBrush(pointFill)");
    return brushes;
}

void drawGeometry(ID2D1Factory& factory, ID2D1RenderTarget& target, const Viewport& viewport,
                   const Brushes& brushes, GeometryType type, const Geometry& geometry) {
    static constexpr float kPointRadius = 3.0f;

    switch (type) {
        case GeometryType::Point:
        case GeometryType::MultiPoint: {
            for (const Part& part : geometry.parts()) {
                for (const Ring& ring : part) {
                    for (const Point2D& p : ring) {
                        const D2D1_POINT_2F center = toD2DPoint(viewport.mapToScreen(p));
                        target.FillEllipse(D2D1::Ellipse(center, kPointRadius, kPointRadius),
                                            brushes.pointFill.Get());
                    }
                }
            }
            break;
        }
        case GeometryType::LineString:
        case GeometryType::MultiLineString: {
            const auto path = buildPathGeometry(factory, viewport, geometry, /*filled=*/false);
            target.DrawGeometry(path.Get(), brushes.lineStroke.Get(), 1.5f);
            break;
        }
        case GeometryType::Polygon:
        case GeometryType::MultiPolygon: {
            const auto path = buildPathGeometry(factory, viewport, geometry, /*filled=*/true);
            target.FillGeometry(path.Get(), brushes.polygonFill.Get());
            target.DrawGeometry(path.Get(), brushes.polygonStroke.Get(), 1.0f);
            break;
        }
        case GeometryType::Unknown:
            break;
    }
}

}  // namespace

void drawDataset(ID2D1RenderTarget& target, ID2D1Factory& factory, const Dataset& dataset,
                  const Viewport& viewport) {
    const Brushes brushes = createBrushes(target);
    for (const Layer& layer : dataset.layers()) {
        for (const Feature& feature : layer.features()) {
            drawGeometry(factory, target, viewport, brushes, feature.geometry().type(), feature.geometry());
        }
    }
}

std::size_t drawDatasetCulled(ID2D1RenderTarget& target, ID2D1Factory& factory, const Dataset& dataset,
                               const std::vector<LayerCache>& layerCaches, const Viewport& viewport,
                               jobs::ThreadPool& pool) {
    const Brushes brushes = createBrushes(target);
    const double mapUnitsPerPixel = viewport.scale() > 0.0 ? 1.0 / viewport.scale() : 0.0;
    const Envelope viewExtent = viewport.mapExtent();
    static constexpr float kPointRadius = 3.0f;

    // Parenthesized (std::min) - see the comment on ThreadPool's constructor
    // in thread_pool.h for why, in a TU that also includes windows.h.
    const std::size_t layerCount = (std::min)(dataset.layers().size(), layerCaches.size());

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
        futures.push_back(pool.submit([&factory, &layerCaches, &viewport, mapUnitsPerPixel, viewExtent, &prepared,
                                        begin, end] {
            for (std::size_t layerIdx = begin; layerIdx < end; ++layerIdx) {
                prepared[layerIdx] =
                    prepareLayer(factory, layerCaches[layerIdx], viewport, mapUnitsPerPixel, viewExtent);
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
    for (const PreparedLayer& layer : prepared) {
        drawnCount += layer.visibleCount;

        for (const D2D1_POINT_2F& center : layer.points) {
            target.FillEllipse(D2D1::Ellipse(center, kPointRadius, kPointRadius), brushes.pointFill.Get());
        }
        if (layer.lineGeometry) {
            target.DrawGeometry(layer.lineGeometry.Get(), brushes.lineStroke.Get(), 1.5f);
        }
        if (layer.polygonGeometry) {
            target.FillGeometry(layer.polygonGeometry.Get(), brushes.polygonFill.Get());
            target.DrawGeometry(layer.polygonGeometry.Get(), brushes.polygonStroke.Get(), 1.0f);
        }
    }

    return drawnCount;
}

OffscreenTarget::OffscreenTarget(ScreenSize size) {
    const ComPtr<IWICImagingFactory> wicFactory = createWicFactory();

    // MULTI_THREADED: shared by Renderer::render (drawDataset, single-
    // threaded regardless - unaffected) and bench --culled (drawDatasetCulled,
    // which needs it - see renderer.h).
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

ComPtr<IWICBitmap> Renderer::render(const Dataset& dataset, const Viewport& viewport) {
    OffscreenTarget target(viewport.screenSize());
    target.beginFrame();
    drawDataset(target.renderTarget(), target.factory(), dataset, viewport);
    target.endFrame();
    return target.bitmap();
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
