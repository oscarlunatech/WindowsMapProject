#include "cartograph/render/renderer.h"

#include <d2d1.h>
#include <d2d1helper.h>
#include <windows.h>

#include <cstddef>

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
                               const std::vector<LayerCache>& layerCaches, const Viewport& viewport) {
    const Brushes brushes = createBrushes(target);
    const double mapUnitsPerPixel = viewport.scale() > 0.0 ? 1.0 / viewport.scale() : 0.0;
    const Envelope& viewExtent = viewport.mapExtent();
    static constexpr float kPointRadius = 3.0f;

    const std::vector<Layer>& layers = dataset.layers();
    std::size_t drawnCount = 0;

    for (std::size_t layerIdx = 0; layerIdx < layers.size() && layerIdx < layerCaches.size(); ++layerIdx) {
        const LayerCache& cache = layerCaches[layerIdx];
        const std::vector<std::size_t> visible = cache.query(viewExtent);
        drawnCount += visible.size();

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
                                const D2D1_POINT_2F center = toD2DPoint(viewport.mapToScreen(p));
                                target.FillEllipse(D2D1::Ellipse(center, kPointRadius, kPointRadius),
                                                    brushes.pointFill.Get());
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
            const auto path = buildPathGeometry(factory, viewport, lineGeoms, /*filled=*/false);
            target.DrawGeometry(path.Get(), brushes.lineStroke.Get(), 1.5f);
        }
        if (!polygonGeoms.empty()) {
            const auto path = buildPathGeometry(factory, viewport, polygonGeoms, /*filled=*/true);
            target.FillGeometry(path.Get(), brushes.polygonFill.Get());
            target.DrawGeometry(path.Get(), brushes.polygonStroke.Get(), 1.0f);
        }
    }

    return drawnCount;
}

OffscreenTarget::OffscreenTarget(ScreenSize size) {
    const ComPtr<IWICImagingFactory> wicFactory = createWicFactory();

    throwIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf()),
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
