#include "viewer.h"

#include <windowsx.h>

#include <d2d1helper.h>

#include <cstdio>
#include <cwchar>
#include <string>

#include "cartograph/render/renderer.h"

using cartograph::Dataset;
using cartograph::Envelope;
using cartograph::Point2D;
using cartograph::render::RenderError;
using cartograph::render::ScreenSize;
using cartograph::render::Viewport;

namespace {

void throwIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        throw RenderError(std::string(what) + " failed (hr=" + std::to_string(hr) + ")");
    }
}

constexpr double kZoomFactorPerNotch = 1.1;
constexpr double kPanFraction = 0.1;
constexpr wchar_t kWindowClassName[] = L"CartographViewerWindowClass";

}  // namespace

Viewer::Viewer(Dataset dataset, Envelope initialExtent)
    : dataset_(std::move(dataset)), mapExtent_(initialExtent) {
    layerCaches_.reserve(dataset_.layers().size());
    for (const auto& layer : dataset_.layers()) {
        totalFeatureCount_ += layer.features().size();
        layerCaches_.emplace_back(layer);
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

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
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
            lastMousePos_ = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP:
            dragging_ = false;
            ReleaseCapture();
            return 0;
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

    throwIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf()),
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

    LARGE_INTEGER freq;
    LARGE_INTEGER start;
    LARGE_INTEGER end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    renderTarget_->BeginDraw();
    renderTarget_->Clear(D2D1::ColorF(D2D1::ColorF::White));
    const std::size_t drawnCount = cartograph::render::drawDatasetCulled(
        *renderTarget_.Get(), *d2dFactory_.Get(), dataset_, layerCaches_, currentViewport());

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
