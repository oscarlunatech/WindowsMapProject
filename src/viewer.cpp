#include "viewer.h"

#include <windowsx.h>

#include <d2d1helper.h>

#include <cstdio>
#include <cwchar>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "cartograph/query/identify.h"
#include "cartograph/render/renderer.h"

using cartograph::Dataset;
using cartograph::Envelope;
using cartograph::Layer;
using cartograph::Point2D;
using cartograph::render::LayerCache;
using cartograph::render::RenderError;
using cartograph::render::ScreenSize;
using cartograph::render::Viewport;

namespace {

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

constexpr double kZoomFactorPerNotch = 1.1;
constexpr double kPanFraction = 0.1;
constexpr long kClickSlopPixelsSquared = 4 * 4;  // movement under 4px still counts as a click
constexpr double kIdentifyRadiusPixels = 5.0;    // click tolerance, converted to map units per-click
constexpr std::size_t kIdentifyMaxFields = 8;    // fields shown in the overlay for the topmost hit
constexpr wchar_t kWindowClassName[] = L"CartographViewerWindowClass";

// Returns an empty string for a null attribute, so callers can skip it.
std::wstring formatAttribute(const cartograph::AttributeValue& value) {
    return std::visit(
        [](const auto& v) -> std::wstring {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return {};
            } else if constexpr (std::is_same_v<T, std::string>) {
                return widen(v);
            } else {
                return std::to_wstring(v);
            }
        },
        value);
}
constexpr UINT kMsgDatasetLoaded = WM_APP + 1;

// Posted from loaderThread_ to the UI thread via PostMessageW/kMsgDatasetLoaded
// (see Viewer::loadInBackground) - the only cross-thread handoff in Viewer,
// and the only reason no mutex is needed: dataset_/layerCaches_/mapExtent_
// are written exclusively by the UI thread, in handleMessage below, once it
// takes ownership of this struct out of the message's LPARAM.
struct LoadResult {
    std::optional<Dataset> dataset;
    std::vector<LayerCache> layerCaches;
    std::optional<cartograph::style::Stylesheet> stylesheet;
    Envelope extent;
    std::size_t featureCount = 0;
    std::wstring errorMessage;  // engaged only when dataset is nullopt
};

}  // namespace

Viewer::Viewer(std::string path, std::string stylePath)
    : datasetPath_(std::move(path)), stylePath_(std::move(stylePath)) {
    loadMessage_ = L"Loading " + widen(datasetPath_) + L"...";
}

Viewer::~Viewer() {
    if (loaderThread_.joinable()) {
        loaderThread_.join();
    }
}

Viewport Viewer::currentViewport() const { return Viewport(mapExtent_, screenSize_); }

void Viewer::run() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Viewer::wndProcTrampoline;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kWindowClassName, L"Cartograph Viewer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                 CW_USEDEFAULT, screenSize_.width, screenSize_.height, nullptr, nullptr,
                                 wc.hInstance, this);
    if (hwnd == nullptr) {
        throw RenderError("CreateWindowExW failed");
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    loaderThread_ = std::thread(&Viewer::loadInBackground, this, hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Viewer::loadInBackground(HWND hwnd) {
    auto result = std::make_unique<LoadResult>();
    try {
        Dataset dataset = Dataset::open(datasetPath_);
        const Envelope extent = dataset.extent();
        std::size_t featureCount = 0;
        for (const Layer& layer : dataset.layers()) {
            featureCount += layer.features().size();
        }

        // buildLayerCachesParallel captures const Layer& references into
        // dataset's own layers while it builds each LayerCache concurrently
        // on pool_ - dataset must not move until that call returns.
        std::vector<LayerCache> caches = cartograph::render::buildLayerCachesParallel(pool_, dataset.layers());

        // Resolving every feature's symbol is O(features), so it belongs here
        // on the loader thread with the rest of the load - not in onPaint. A
        // bad style file surfaces through the same error overlay as a bad
        // dataset path.
        cartograph::style::Stylesheet stylesheet =
            stylePath_.empty()
                ? cartograph::style::Stylesheet::defaults(dataset)
                : cartograph::style::Stylesheet(cartograph::style::loadStyleSpec(stylePath_), dataset);

        result->dataset = std::move(dataset);
        result->layerCaches = std::move(caches);
        result->stylesheet = std::move(stylesheet);
        result->extent = extent;
        result->featureCount = featureCount;
    } catch (const std::exception& e) {
        result->errorMessage = widen(e.what());
    }
    PostMessageW(hwnd, kMsgDatasetLoaded, 0, reinterpret_cast<LPARAM>(result.release()));
}

LRESULT CALLBACK Viewer::wndProcTrampoline(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    auto* self = reinterpret_cast<Viewer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->handleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Viewer::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            createDeviceResources(hwnd);
            return 0;
        case WM_PAINT:
            onPaint(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;  // D2D repaints the whole client area every frame; skip GDI's clear.
        case WM_SIZE:
            onResize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_LBUTTONDOWN:
            dragging_ = true;
            dragMoved_ = false;
            lastMousePos_ = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP: {
            const bool wasClick = dragging_ && !dragMoved_;
            dragging_ = false;
            ReleaseCapture();
            if (wasClick) {
                onClick(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            onMouseMove(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_MOUSEWHEEL: {
            // WM_MOUSEWHEEL reports screen coordinates, unlike other mouse messages.
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), pt);
            return 0;
        }
        case WM_KEYDOWN:
            onKeyDown(wParam);
            return 0;
        case kMsgDatasetLoaded: {
            std::unique_ptr<LoadResult> result(reinterpret_cast<LoadResult*>(lParam));
            if (result->dataset) {
                dataset_ = std::move(result->dataset);
                layerCaches_ = std::move(result->layerCaches);
                stylesheet_ = std::move(result->stylesheet);
                totalFeatureCount_ = result->featureCount;
                mapExtent_ = result->extent;
                loadState_ = LoadState::Ready;
            } else {
                loadState_ = LoadState::Failed;
                loadMessage_ = L"Failed to load " + widen(datasetPath_) + L": " + result->errorMessage;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void Viewer::createDeviceResources(HWND hwnd) {
    if (renderTarget_) {
        return;
    }

    // MULTI_THREADED (not SINGLE_THREADED): drawDatasetCulled builds each
    // layer's ID2D1PathGeometry on a pool worker thread - see the comment on
    // drawDatasetCulled in renderer.h. ID2D1RenderTarget itself still only
    // gets touched from this (the UI) thread regardless.
    throwIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, d2dFactory_.GetAddressOf()),
                  "D2D1CreateFactory");
    throwIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf())),
                  "DWriteCreateFactory");
    throwIfFailed(writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f,
                                                   L"en-us", &textFormat_),
                  "CreateTextFormat");

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    screenSize_ = ScreenSize{static_cast<int>(clientRect.right - clientRect.left),
                              static_cast<int>(clientRect.bottom - clientRect.top)};

    const D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    const D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        hwnd, D2D1::SizeU(static_cast<UINT32>(screenSize_.width), static_cast<UINT32>(screenSize_.height)));
    throwIfFailed(d2dFactory_->CreateHwndRenderTarget(rtProps, hwndProps, &renderTarget_),
                  "CreateHwndRenderTarget");

    throwIfFailed(renderTarget_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &overlayBrush_),
                  "CreateSolidColorBrush(overlay)");
}

void Viewer::onResize(UINT width, UINT height) {
    screenSize_ = ScreenSize{static_cast<int>(width), static_cast<int>(height)};
    if (renderTarget_) {
        renderTarget_->Resize(D2D1::SizeU(width, height));
    }
}

void Viewer::onPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);

    renderTarget_->BeginDraw();
    renderTarget_->Clear(D2D1::ColorF(D2D1::ColorF::White));

    if (loadState_ != LoadState::Ready) {
        const D2D1_RECT_F layoutRect = D2D1::RectF(8.0f, 8.0f, static_cast<float>(screenSize_.width) - 8.0f,
                                                     static_cast<float>(screenSize_.height) - 8.0f);
        renderTarget_->DrawText(loadMessage_.c_str(), static_cast<UINT32>(loadMessage_.size()), textFormat_.Get(),
                                 layoutRect, overlayBrush_.Get());
    } else {
        LARGE_INTEGER freq;
        LARGE_INTEGER start;
        LARGE_INTEGER end;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);

        const std::size_t drawnCount =
            cartograph::render::drawDatasetCulled(*renderTarget_.Get(), *d2dFactory_.Get(), *dataset_,
                                                   layerCaches_, currentViewport(), pool_, *stylesheet_);

        QueryPerformanceCounter(&end);
        const double ms =
            1000.0 * static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(freq.QuadPart);

        wchar_t overlay[256];
        swprintf_s(overlay, L"ms/frame: %.2f   features drawn: %zu   culled: %zu", ms, drawnCount,
                   totalFeatureCount_ - drawnCount);
        const D2D1_RECT_F layoutRect =
            D2D1::RectF(8.0f, 8.0f, static_cast<float>(screenSize_.width) - 8.0f, 32.0f);
        renderTarget_->DrawText(overlay, static_cast<UINT32>(wcslen(overlay)), textFormat_.Get(), layoutRect,
                                 overlayBrush_.Get());

        if (!identifyText_.empty()) {
            const D2D1_RECT_F identifyRect = D2D1::RectF(8.0f, 34.0f,
                                                          static_cast<float>(screenSize_.width) - 8.0f,
                                                          static_cast<float>(screenSize_.height) - 8.0f);
            renderTarget_->DrawText(identifyText_.c_str(), static_cast<UINT32>(identifyText_.size()),
                                     textFormat_.Get(), identifyRect, overlayBrush_.Get());
        }
    }

    const HRESULT hr = renderTarget_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        renderTarget_.Reset();
        createDeviceResources(hwnd);
    }

    EndPaint(hwnd, &ps);
    InvalidateRect(hwnd, nullptr, FALSE);  // keep redrawing continuously
}

void Viewer::onMouseMove(POINT clientPos) {
    if (!dragging_) {
        return;
    }

    // A press-and-release that never travels more than a few pixels is a
    // click (identify), not a pan - without this, every click would also
    // nudge the map by a pixel or two of hand-shake.
    const long dxPixels = clientPos.x - lastMousePos_.x;
    const long dyPixels = clientPos.y - lastMousePos_.y;
    if (dxPixels * dxPixels + dyPixels * dyPixels > kClickSlopPixelsSquared) {
        dragMoved_ = true;
    }
    if (!dragMoved_) {
        return;  // hold position until it's clearly a drag
    }

    const Viewport viewport = currentViewport();
    const Point2D lastMap = viewport.screenToMap(
        Point2D{static_cast<double>(lastMousePos_.x), static_cast<double>(lastMousePos_.y)});
    const Point2D currentMap =
        viewport.screenToMap(Point2D{static_cast<double>(clientPos.x), static_cast<double>(clientPos.y)});

    const double dx = lastMap.x - currentMap.x;
    const double dy = lastMap.y - currentMap.y;
    mapExtent_.minX += dx;
    mapExtent_.maxX += dx;
    mapExtent_.minY += dy;
    mapExtent_.maxY += dy;

    lastMousePos_ = clientPos;
}

void Viewer::onClick(POINT clientPos) {
    if (loadState_ != LoadState::Ready) {
        return;
    }

    const Viewport viewport = currentViewport();
    const Point2D mapPoint =
        viewport.screenToMap(Point2D{static_cast<double>(clientPos.x), static_cast<double>(clientPos.y)});

    // A fixed pixel radius is the right unit for a click target, so convert it
    // to map units against the current zoom rather than picking a map-unit
    // tolerance that would be enormous zoomed out and useless zoomed in.
    const double tolerance =
        viewport.scale() > 0.0 ? kIdentifyRadiusPixels / viewport.scale() : 0.0;

    const std::vector<cartograph::query::Hit> hits =
        cartograph::query::identify(*dataset_, layerCaches_, mapPoint, tolerance);

    if (hits.empty()) {
        identifyText_ = L"identify: nothing here";
        return;
    }

    const cartograph::query::Hit& hit = hits.front();
    const Layer& layer = dataset_->layers()[hit.layerIndex];
    const cartograph::Feature& feature = layer.features()[hit.featureIndex];

    std::wstring text = widen(layer.name()) + L"  feature " + std::to_wstring(feature.id());
    if (hits.size() > 1) {
        text += L"   (+" + std::to_wstring(hits.size() - 1) + L" more here)";
    }

    std::size_t shown = 0;
    for (std::size_t i = 0; i < layer.fields().size() && i < feature.attributes().size(); ++i) {
        if (shown >= kIdentifyMaxFields) {
            break;
        }
        const std::wstring value = formatAttribute(feature.attributes()[i]);
        if (value.empty()) {
            continue;  // skip nulls; overlay space is scarce
        }
        text += L"\n" + widen(layer.fields()[i].name) + L": " + value;
        ++shown;
    }
    identifyText_ = std::move(text);
}

void Viewer::onMouseWheel(short wheelDelta, POINT clientPos) {
    const Viewport viewport = currentViewport();
    const Point2D anchor = viewport.screenToMap(
        Point2D{static_cast<double>(clientPos.x), static_cast<double>(clientPos.y)});
    // Wheel pushed forward (positive delta) zooms in, i.e. shrinks the extent.
    const double factor = wheelDelta > 0 ? (1.0 / kZoomFactorPerNotch) : kZoomFactorPerNotch;
    zoomAt(anchor, factor);
}

void Viewer::zoomAt(Point2D anchor, double factor) {
    mapExtent_.minX = anchor.x + (mapExtent_.minX - anchor.x) * factor;
    mapExtent_.maxX = anchor.x + (mapExtent_.maxX - anchor.x) * factor;
    mapExtent_.minY = anchor.y + (mapExtent_.minY - anchor.y) * factor;
    mapExtent_.maxY = anchor.y + (mapExtent_.maxY - anchor.y) * factor;
}

void Viewer::onKeyDown(WPARAM key) {
    const double dx = mapExtent_.width() * kPanFraction;
    const double dy = mapExtent_.height() * kPanFraction;

    switch (key) {
        case VK_LEFT:
            mapExtent_.minX -= dx;
            mapExtent_.maxX -= dx;
            break;
        case VK_RIGHT:
            mapExtent_.minX += dx;
            mapExtent_.maxX += dx;
            break;
        case VK_UP:
            mapExtent_.minY += dy;
            mapExtent_.maxY += dy;
            break;
        case VK_DOWN:
            mapExtent_.minY -= dy;
            mapExtent_.maxY -= dy;
            break;
        case VK_OEM_PLUS:
        case VK_ADD:
            zoomAt(Point2D{(mapExtent_.minX + mapExtent_.maxX) / 2.0, (mapExtent_.minY + mapExtent_.maxY) / 2.0},
                   1.0 / kZoomFactorPerNotch);
            break;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            zoomAt(Point2D{(mapExtent_.minX + mapExtent_.maxX) / 2.0, (mapExtent_.minY + mapExtent_.maxY) / 2.0},
                   kZoomFactorPerNotch);
            break;
        default:
            break;
    }
}
