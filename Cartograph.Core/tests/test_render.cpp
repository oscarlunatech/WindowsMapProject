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

// Pixels come back as 32bpp PBGRA, so the channel order here is B, G, R.
std::size_t countPixels(const std::vector<std::uint8_t>& pixels, std::uint8_t b, std::uint8_t g,
                         std::uint8_t r) {
    std::size_t count = 0;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (pixels[i] == b && pixels[i + 1] == g && pixels[i + 2] == r) {
            ++count;
        }
    }
    return count;
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

TEST_CASE("the culled draw path draws each category with its own symbol", "[render]") {
    // The golden-image test above only covers drawDataset (the naive path).
    // This covers drawDatasetCulled, where symbology has to survive being
    // grouped into one batched ID2D1PathGeometry per symbol - the part of
    // Phase 7 that could plausibly draw a whole layer in one category's color.
    const Dataset dataset = Dataset::open(fixturePath("ne_110m_admin_0_countries.shp"));

    style::Symbol africa;
    africa.fill = style::Color{1.0f, 0.0f, 0.0f, 1.0f};
    africa.polygonStrokeWidth = 0.0f;  // no outline, so interior pixels are exactly the fill
    style::Symbol europe;
    europe.fill = style::Color{0.0f, 1.0f, 0.0f, 1.0f};
    europe.polygonStrokeWidth = 0.0f;

    style::Categorized categorized;
    categorized.field = "CONTINENT";
    categorized.categories = {{std::string("Africa"), africa}, {std::string("Europe"), europe}};
    categorized.fallback = style::Symbol{};

    style::StyleSpec spec;
    spec.byLayerName.emplace("ne_110m_admin_0_countries", categorized);
    const style::Stylesheet stylesheet(spec, dataset);

    Envelope bbox;
    bbox.expand(Point2D{-180, -90});
    bbox.expand(Point2D{180, 90});
    const Viewport viewport(bbox, ScreenSize{800, 400});

    jobs::ThreadPool pool;
    const std::vector<LayerCache> caches = buildLayerCachesParallel(pool, dataset.layers());

    OffscreenTarget target(viewport.screenSize());
    target.beginFrame();
    drawDatasetCulled(target.renderTarget(), target.factory(), dataset, caches, viewport, pool, stylesheet);
    target.endFrame();
    const std::vector<std::uint8_t> pixels = readPixels(target.bitmap().Get());

    // Both categories present, in their own color, at the same time.
    REQUIRE(countPixels(pixels, 0, 0, 255) > 1000);  // Africa, pure red
    REQUIRE(countPixels(pixels, 0, 255, 0) > 1000);  // Europe, pure green

    // Control: the default stylesheet paints neither, so the counts above are
    // really coming from the style and not from something inherent to the data.
    OffscreenTarget plain(viewport.screenSize());
    plain.beginFrame();
    drawDatasetCulled(plain.renderTarget(), plain.factory(), dataset, caches, viewport, pool,
                      style::Stylesheet::defaults(dataset));
    plain.endFrame();
    const std::vector<std::uint8_t> plainPixels = readPixels(plain.bitmap().Get());

    REQUIRE(countPixels(plainPixels, 0, 0, 255) == 0);
    REQUIRE(countPixels(plainPixels, 0, 255, 0) == 0);
}
