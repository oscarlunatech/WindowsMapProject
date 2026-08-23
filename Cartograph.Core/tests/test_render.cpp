#include <catch2/catch_test_macros.hpp>

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#include "cartograph/dataset.h"
#include "cartograph/render/renderer.h"

using namespace cartograph;
using namespace cartograph::render;
using Microsoft::WRL::ComPtr;

namespace {

std::string fixturePath(const std::string& name) {
    return std::string(CARTOGRAPH_TEST_FIXTURES_DIR) + "/" + name;
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
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> factory;
    REQUIRE(SUCCEEDED(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))));
    return factory;
}

std::vector<std::uint8_t> readPixels(IWICBitmapSource* source) {
    UINT width = 0;
    UINT height = 0;
    REQUIRE(SUCCEEDED(source->GetSize(&width, &height)));

    const UINT stride = width * 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height);
    REQUIRE(SUCCEEDED(
        source->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data())));
    return pixels;
}

std::vector<std::uint8_t> decodePng(const std::string& path) {
    const ComPtr<IWICImagingFactory> wicFactory = createWicFactory();

    ComPtr<IWICBitmapDecoder> decoder;
    REQUIRE(SUCCEEDED(wicFactory->CreateDecoderFromFilename(widen(path).c_str(), nullptr, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnDemand, &decoder)));

    ComPtr<IWICBitmapFrameDecode> frame;
    REQUIRE(SUCCEEDED(decoder->GetFrame(0, &frame)));

    ComPtr<IWICFormatConverter> converter;
    REQUIRE(SUCCEEDED(wicFactory->CreateFormatConverter(&converter)));
    REQUIRE(SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                             WICBitmapDitherTypeNone, nullptr, 0.0,
                                             WICBitmapPaletteTypeCustom)));

    return readPixels(converter.Get());
}

}  // namespace

TEST_CASE("rendering the countries fixture matches the committed golden image", "[render]") {
    const Dataset dataset = Dataset::open(fixturePath("ne_110m_admin_0_countries.shp"));

    Envelope bbox;
    bbox.expand(Point2D{-180, -90});
    bbox.expand(Point2D{180, 90});
    const Viewport viewport(bbox, ScreenSize{800, 400});

    const ComPtr<IWICBitmap> rendered = Renderer::render(dataset, viewport);
    const std::vector<std::uint8_t> renderedPixels = readPixels(rendered.Get());
    const std::vector<std::uint8_t> goldenPixels = decodePng(fixturePath("golden/countries_world.png"));

    REQUIRE(renderedPixels.size() == goldenPixels.size());
    REQUIRE(renderedPixels == goldenPixels);
}
