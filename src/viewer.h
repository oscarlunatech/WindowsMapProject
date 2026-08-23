#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "cartograph/dataset.h"
#include "cartograph/render/viewport.h"

// Opens a live Win32 window rendering a Dataset with mouse pan, scroll-wheel
// zoom, keyboard nav, and a frame-timing overlay. Deliberately outside
// Cartograph.Core - the core knows nothing about windows or message loops;
// it only supplies drawDataset() (see cartograph/render/renderer.h), which
// this class calls into every frame.
class Viewer {
public:
    Viewer(cartograph::Dataset dataset, cartograph::Envelope initialExtent);

    void run();

private:
    static LRESULT CALLBACK wndProcTrampoline(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void createDeviceResources(HWND hwnd);
    void onPaint(HWND hwnd);
    void onResize(UINT width, UINT height);
    void onMouseMove(POINT clientPos);
    void onMouseWheel(short wheelDelta, POINT clientPos);
    void onKeyDown(WPARAM key);
    void zoomAt(cartograph::Point2D anchorMap, double factor);

    cartograph::render::Viewport currentViewport() const;

    cartograph::Dataset dataset_;
    std::size_t totalFeatureCount_ = 0;
    cartograph::Envelope mapExtent_;
    cartograph::render::ScreenSize screenSize_{1024, 768};

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> overlayBrush_;

    bool dragging_ = false;
    POINT lastMousePos_{};
};
