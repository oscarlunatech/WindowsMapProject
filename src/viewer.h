#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "cartograph/jobs/thread_pool.h"
#include "cartograph/map.h"
#include "cartograph/render/viewport.h"
#include "cartograph/style/stylesheet.h"

// Opens a live Win32 window rendering a Map with mouse pan, scroll-wheel
// zoom, click-to-identify, keyboard nav, and a frame-timing overlay.
// Deliberately outside Cartograph.Core - the core knows nothing about windows
// or message loops; it only supplies drawMapCulled() (see
// cartograph/render/renderer.h), which this class calls every frame.
//
// The map is opened on a background thread (loaderThread_) so the window
// appears immediately instead of blocking on Map::open() - see
// loadInBackground() in viewer.cpp. pool_ serves both that one-off load and
// the per-frame parallel layer-prep work inside drawMapCulled.
class Viewer {
public:
    // paths stack as layers, first path at the bottom. stylePath is a JSON
    // style file (see cartograph/style/style_spec.h), or empty for the
    // built-in default symbology; it's read and bound to the map on
    // loaderThread_ along with everything else, since style::Stylesheet's
    // constructor is O(features).
    explicit Viewer(std::vector<std::string> paths, std::string displayCrs, std::string stylePath = {});
    ~Viewer();

    Viewer(const Viewer&) = delete;
    Viewer& operator=(const Viewer&) = delete;

    void run();

private:
    enum class LoadState { Loading, Ready, Failed };

    static LRESULT CALLBACK wndProcTrampoline(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void createDeviceResources(HWND hwnd);
    void onPaint(HWND hwnd);
    void onResize(UINT width, UINT height);
    void onMouseMove(POINT clientPos);
    void onMouseWheel(short wheelDelta, POINT clientPos);
    void onClick(POINT clientPos);  // a left-button press+release that didn't pan
    void onKeyDown(WPARAM key);
    void zoomAt(cartograph::Point2D anchorMap, double factor);
    void loadInBackground(HWND hwnd);  // runs on loaderThread_

    // Raster layers are re-read at screen resolution whenever the view moves
    // far enough to matter, so they stay sharp at every zoom. The read is file
    // I/O and must never happen in onPaint, so it runs on rasterThread_ and
    // the finished images come back through the message queue - the same
    // handoff loadInBackground uses.
    void maybeStartRasterRead(HWND hwnd);
    void readRastersInBackground(HWND hwnd, cartograph::Envelope window,
                                  cartograph::render::ScreenSize size);
    bool rasterReadWorthwhile(const cartograph::Envelope& current) const;

    cartograph::render::Viewport currentViewport() const;

    std::vector<std::string> datasetPaths_;
    std::string displayCrs_;
    std::string stylePath_;  // empty means default symbology
    cartograph::jobs::ThreadPool pool_;
    std::thread loaderThread_;
    LoadState loadState_ = LoadState::Loading;
    std::wstring loadMessage_;  // overlay text while loadState_ != Ready; set in the constructor and on failure

    std::optional<cartograph::Map> map_;                       // engaged once loadState_ == Ready
    std::size_t totalFeatureCount_ = 0;
    std::optional<cartograph::style::Stylesheet> stylesheet_;  // engaged alongside map_, built once
    cartograph::Envelope mapExtent_;
    cartograph::render::ScreenSize screenSize_{1024, 768};

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> overlayBrush_;

    bool dragging_ = false;
    bool dragMoved_ = false;  // distinguishes a click (identify) from a drag (pan)
    POINT lastMousePos_{};

    std::wstring identifyText_;  // overlay text from the last click; empty until one happens

    std::thread rasterThread_;             // joined before the next read starts, and in the destructor
    bool rasterReadInFlight_ = false;      // UI thread only
    bool anyRasterLayers_ = false;         // set when the map loads; skips all of this when false
    cartograph::Envelope rasterReadExtent_;  // the view the in-flight or latest read covers
};
