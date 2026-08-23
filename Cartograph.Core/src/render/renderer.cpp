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

// One figure per ring; filled+closed for polygons, hollow+open for lines.
// Fill mode is alternate so polygon holes render correctly regardless of
// ring winding order.
ComPtr<ID2D1PathGeometry> buildPathGeometry(ID2D1Factory* factory, const Viewport& viewport,
                                             const Geometry& geometry, bool filled) {
    ComPtr<ID2D1PathGeometry> path;
    throwIfFailed(factory->CreatePathGeometry(&path), "CreatePathGeometry");

    ComPtr<ID2D1GeometrySink> sink;
    throwIfFailed(path->Open(&sink), "ID2D1PathGeometry::Open");
    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);

    const D2D1_FIGURE_BEGIN beginMode = filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW;
    const D2D1_FIGURE_END endMode = filled ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN;

    for (const Part& part : geometry.parts()) {
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

    throwIfFailed(sink->Close(), "ID2D1GeometrySink::Close");
    return path;
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

void drawFeature(ID2D1Factory& factory, ID2D1RenderTarget& target, const Viewport& viewport,
                  const Brushes& brushes, const Feature& feature) {
    static constexpr float kPointRadius = 3.0f;

    const Geometry& geometry = feature.geometry();
    switch (geometry.type()) {
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
            const auto path = buildPathGeometry(&factory, viewport, geometry, /*filled=*/false);
            target.DrawGeometry(path.Get(), brushes.lineStroke.Get(), 1.5f);
            break;
        }
        case GeometryType::Polygon:
        case GeometryType::MultiPolygon: {
            const auto path = buildPathGeometry(&factory, viewport, geometry, /*filled=*/true);
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
            drawFeature(factory, target, viewport, brushes, feature);
        }
    }
}

ComPtr<IWICBitmap> Renderer::render(const Dataset& dataset, const Viewport& viewport) {
    const ComPtr<IWICImagingFactory> wicFactory = createWicFactory();

    ComPtr<ID2D1Factory> d2dFactory;
    throwIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf()),
                  "D2D1CreateFactory");

    ComPtr<IWICBitmap> wicBitmap;
    throwIfFailed(wicFactory->CreateBitmap(static_cast<UINT>(viewport.screenSize().width),
                                            static_cast<UINT>(viewport.screenSize().height),
                                            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand,
                                            &wicBitmap),
                  "IWICImagingFactory::CreateBitmap");

    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1RenderTarget> renderTarget;
    throwIfFailed(d2dFactory->CreateWicBitmapRenderTarget(wicBitmap.Get(), props, &renderTarget),
                  "CreateWicBitmapRenderTarget");

    renderTarget->BeginDraw();
    renderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));
    drawDataset(*renderTarget.Get(), *d2dFactory.Get(), dataset, viewport);
    throwIfFailed(renderTarget->EndDraw(), "ID2D1RenderTarget::EndDraw");

    return wicBitmap;
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
