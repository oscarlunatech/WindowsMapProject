#include "stylesheet.h"

#include <string>

#include "cartograph/style/style_spec.h"

#include "conversions.h"
#include "errors.h"

using namespace System;

namespace Cartograph {
namespace Interop {

Stylesheet ^ Stylesheet::Defaults(Map ^ map) {
    if (map == nullptr) {
        throw gcnew ArgumentNullException("map");
    }
    cartograph::Map& nativeMap = map->Native();
    CARTOGRAPH_TRY
        return gcnew Stylesheet(
            new cartograph::style::Stylesheet(cartograph::style::Stylesheet::defaults(nativeMap)));
    CARTOGRAPH_CATCH
}

Stylesheet ^ Stylesheet::FromFile(String ^ path, Map ^ map) {
    if (map == nullptr) {
        throw gcnew ArgumentNullException("map");
    }
    const std::string nativePath = Detail::ToNative(path);
    cartograph::Map& nativeMap = map->Native();
    CARTOGRAPH_TRY
        const cartograph::style::StyleSpec spec = cartograph::style::loadStyleSpec(nativePath);
        return gcnew Stylesheet(new cartograph::style::Stylesheet(spec, nativeMap));
    CARTOGRAPH_CATCH
}

Stylesheet ^ Stylesheet::FromJson(String ^ json, Map ^ map) {
    if (map == nullptr) {
        throw gcnew ArgumentNullException("map");
    }
    const std::string nativeJson = Detail::ToNative(json);
    cartograph::Map& nativeMap = map->Native();
    CARTOGRAPH_TRY
        const cartograph::style::StyleSpec spec = cartograph::style::parseStyleSpec(nativeJson);
        return gcnew Stylesheet(new cartograph::style::Stylesheet(spec, nativeMap));
    CARTOGRAPH_CATCH
}

Stylesheet::!Stylesheet() {
    delete native_;
    native_ = nullptr;
}

cartograph::style::Stylesheet& Stylesheet::Native() {
    if (native_ == nullptr) {
        throw gcnew ObjectDisposedException("Stylesheet");
    }
    return *native_;
}

int Stylesheet::SymbolCount::get() {
    return Detail::ToInt(Native().symbolCount());
}

Symbol Stylesheet::GetSymbol(int symbolIndex) {
    cartograph::style::Stylesheet& stylesheet = Native();
    const std::size_t index = Detail::ToIndex(symbolIndex, stylesheet.symbolCount(), "symbolIndex");
    return Detail::ToManaged(stylesheet.symbol(index));
}

int Stylesheet::LayerCount::get() {
    return Detail::ToInt(Native().layerCount());
}

int Stylesheet::SymbolIndex(int layerIndex, int featureIndex) {
    cartograph::style::Stylesheet& stylesheet = Native();
    const std::size_t layer = Detail::ToIndex(layerIndex, stylesheet.layerCount(), "layerIndex");
    // Core indexes straight into a flat table with no bounds check of its own
    // (that is the whole point - it is the renderer's per-feature hot path), so
    // the check has to happen here before the index reaches it.
    const std::size_t featureCount = stylesheet.featureCount(layer);
    const std::size_t feature = Detail::ToIndex(featureIndex, featureCount, "featureIndex");
    return Detail::ToInt(stylesheet.symbolIndex(layer, feature));
}

}  // namespace Interop
}  // namespace Cartograph
