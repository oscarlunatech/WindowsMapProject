#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "cartograph/dataset.h"
#include "cartograph/jobs/thread_pool.h"
#include "cartograph/render/layer_cache.h"
#include "cartograph/render/viewport.h"
#include "cartograph/style/stylesheet.h"

// Opens a live Win32 window rendering a Dataset with mouse pan, scroll-wheel
// zoom, keyboard nav, and a frame-timing overlay. Deliberately outside
// Cartograph.Core - the core knows nothing about windows or message loops;
// it only supplies drawDatasetCulled() (see cartograph/render/renderer.h),
// which this class calls into every frame using a LayerCache built once per
// layer at startup.
//
// The dataset itself is opened and its LayerCaches built on a background
// thread (loaderThread_) so the window appears immediately instead of
// blocking on Dataset::open() - see loadInBackground() in viewer.cpp. pool_
// serves both that one-off load and the per-frame parallel layer-prep work
// inside drawDatasetCulled.
class Viewer {
public:
    // stylePath is a JSON style file (see cartograph/style/style_spec.h), or
    // empty for the built-in default symbology. It's read and bound to the
    // dataset on loaderThread_ along with everything else, since
    // style::Stylesheet's constructor is O(features).
    explicit Viewer(std::string path, std::string stylePath = {});
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

    cartograph::render::Viewport currentViewport() const;

    std::string datasetPath_;
    std::string stylePath_;  // empty means default symbology
    cartograph::jobs::ThreadPool pool_;
    std::thread loaderThread_;
    LoadState loadState_ = LoadState::Loading;
    std::wstring loadMessage_;  // overlay text while loadState_ != Ready; set in the constructor and on failure

    std::optional<cartograph::Dataset> dataset_;  // engaged once loadState_ == Ready
    std::size_t totalFeatureCount_ = 0;
    std::vector<cartograph::render::LayerCache> layerCaches_;  // one per dataset_->layers(), built once
    std::optional<cartograph::style::Stylesheet> stylesheet_;  // engaged alongside dataset_, built once
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
};
