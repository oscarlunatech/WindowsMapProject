#include <catch2/catch_test_macros.hpp>

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#include "cartograph/map.h"
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
    // EPSG:4326 explicitly, not the default. Since Phase 10 the default
    // display CRS is EPSG:3857, which would reproject this fixture and
    // invalidate the golden image. Asking for 4326 keeps the source data's
    // own CRS, which crs::Transformer short-circuits to an exact identity -
    // that is what keeps this comparison byte-exact across the phase.
    const Map map = Map::open(fixturePath("ne_110m_admin_0_countries.shp"), "EPSG:4326");

    Envelope bbox;
    bbox.expand(Point2D{-180, -90});
    bbox.expand(Point2D{180, 90});
    const Viewport viewport(bbox, ScreenSize{800, 400});

    const ComPtr<IWICBitmap> rendered = Renderer::render(map, viewport);
    const std::vector<std::uint8_t> renderedPixels = readPixels(rendered.Get());
    const std::vector<std::uint8_t> goldenPixels = decodePng(fixturePath("golden/countries_world.png"));

    REQUIRE(renderedPixels.size() == goldenPixels.size());
    REQUIRE(renderedPixels == goldenPixels);
}

TEST_CASE("the culled draw path draws each category with its own symbol", "[render]") {
    // The golden-image test above only covers drawMap (the naive path). This
    // covers drawMapCulled, where symbology has to survive being grouped into
    // one batched ID2D1PathGeometry per symbol - the part of Phase 7 that
    // could plausibly draw a whole layer in one category's color.
    //
    // EPSG:4326 so the bbox below can stay in degrees - this is a symbology
    // test, not a projection one.
    const Map map = Map::open(fixturePath("ne_110m_admin_0_countries.shp"), "EPSG:4326");

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
    const style::Stylesheet stylesheet(spec, map);

    Envelope bbox;
    bbox.expand(Point2D{-180, -90});
    bbox.expand(Point2D{180, 90});
    const Viewport viewport(bbox, ScreenSize{800, 400});

    jobs::ThreadPool pool;

    OffscreenTarget target(viewport.screenSize());
    target.beginFrame();
    drawMapCulled(target.renderTarget(), target.factory(), map, viewport, pool, stylesheet);
    target.endFrame();
    const std::vector<std::uint8_t> pixels = readPixels(target.bitmap().Get());

    // Both categories present, in their own color, at the same time.
    REQUIRE(countPixels(pixels, 0, 0, 255) > 1000);  // Africa, pure red
    REQUIRE(countPixels(pixels, 0, 255, 0) > 1000);  // Europe, pure green

    // Control: the default stylesheet paints neither, so the counts above are
    // really coming from the style and not from something inherent to the data.
    OffscreenTarget plain(viewport.screenSize());
    plain.beginFrame();
    drawMapCulled(plain.renderTarget(), plain.factory(), map, viewport, pool,
                  style::Stylesheet::defaults(map));
    plain.endFrame();
    const std::vector<std::uint8_t> plainPixels = readPixels(plain.bitmap().Get());

    REQUIRE(countPixels(plainPixels, 0, 0, 255) == 0);
    REQUIRE(countPixels(plainPixels, 0, 255, 0) == 0);
}

TEST_CASE("a hidden layer draws nothing and an opaque one covers what's below", "[render]") {
    // Two copies of the fixture stacked. Both layers share a name (it's the
    // same file twice), so a name-keyed style can't tell them apart - the
    // "default" style applies to both, which is fine here since what's under
    // test is visibility and opacity, not styling. Outlines are off so
    // interior pixels are exactly the fill color.
    // EPSG:4326 so the bbox below can stay in degrees - what's under test is
    // visibility and opacity, not projection.
    const std::string path = fixturePath("ne_110m_admin_0_countries.shp");
    Map stacked = Map::open(std::vector<std::string>{path, path}, "EPSG:4326");
    REQUIRE(stacked.layers().size() == 2);

    style::Symbol green;
    green.fill = style::Color{0.0f, 1.0f, 0.0f, 1.0f};
    green.polygonStrokeWidth = 0.0f;

    style::StyleSpec greenSpec;
    greenSpec.defaultStyle = style::SingleSymbol{green};

    Envelope bbox;
    bbox.expand(Point2D{-180, -90});
    bbox.expand(Point2D{180, 90});
    const Viewport viewport(bbox, ScreenSize{400, 200});
    jobs::ThreadPool pool;

    const auto renderWith = [&](const style::StyleSpec& spec, const Map& m) {
        const style::Stylesheet sheet(spec, m);
        OffscreenTarget target(viewport.screenSize());
        target.beginFrame();
        drawMapCulled(target.renderTarget(), target.factory(), m, viewport, pool, sheet);
        target.endFrame();
        return readPixels(target.bitmap().Get());
    };

    // Both layers green, both visible: green is present.
    const std::vector<std::uint8_t> bothVisible = renderWith(greenSpec, stacked);
    const std::size_t greenWhenVisible = countPixels(bothVisible, 0, 255, 0);
    REQUIRE(greenWhenVisible > 1000);

    // Hide both layers: nothing of the map is drawn at all.
    stacked.layers()[0].setVisible(false);
    stacked.layers()[1].setVisible(false);
    const std::vector<std::uint8_t> allHidden = renderWith(greenSpec, stacked);
    REQUIRE(countPixels(allHidden, 0, 255, 0) == 0);

    // One layer back on: it draws again, so visibility is per-layer and not
    // an all-or-nothing switch.
    //
    // Deliberately not asserting equality with greenWhenVisible. Drawing the
    // same geometry twice is *not* idempotent under antialiasing: a partially
    // covered edge pixel blended twice lands closer to pure green than one
    // blended once, so the two-layer render has slightly more exactly-green
    // pixels (~2% here) than the one-layer render. Interiors are identical -
    // only antialiased edges differ - so this checks the same order of
    // magnitude rather than an exact count.
    stacked.layers()[0].setVisible(true);
    const std::vector<std::uint8_t> oneVisible = renderWith(greenSpec, stacked);
    const std::size_t greenWithOne = countPixels(oneVisible, 0, 255, 0);
    REQUIRE(greenWithOne > 1000);
    REQUIRE(greenWithOne <= greenWhenVisible);
    REQUIRE(greenWithOne > greenWhenVisible * 9 / 10);

    // A fully transparent layer contributes no opaque pixels of its own.
    stacked.layers()[0].setOpacity(0.0f);
    const std::vector<std::uint8_t> transparent = renderWith(greenSpec, stacked);
    REQUIRE(countPixels(transparent, 0, 255, 0) == 0);
}

TEST_CASE("MapLayer clamps opacity to 0..1", "[render]") {
    Map map = Map::open(fixturePath("ne_110m_admin_0_countries.shp"));
    MapLayer& layer = map.layers().front();

    layer.setOpacity(2.5f);
    REQUIRE(layer.opacity() == 1.0f);
    layer.setOpacity(-1.0f);
    REQUIRE(layer.opacity() == 0.0f);
    layer.setOpacity(0.25f);
    REQUIRE(layer.opacity() == 0.25f);
}
