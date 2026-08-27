#include "viewport.h"

#include "conversions.h"
#include "errors.h"

using namespace System;

namespace Cartograph {
namespace Interop {

Viewport::Viewport(Envelope mapExtent, ScreenSize screenSize) : native_(nullptr) {
    const cartograph::Envelope extent = Detail::ToNative(mapExtent);
    const cartograph::render::ScreenSize size = Detail::ToNative(screenSize);
    CARTOGRAPH_TRY
        native_ = new cartograph::render::Viewport(extent, size);
    CARTOGRAPH_CATCH
}

Viewport::!Viewport() {
    delete native_;
    native_ = nullptr;
}

cartograph::render::Viewport& Viewport::Native() {
    if (native_ == nullptr) {
        throw gcnew ObjectDisposedException("Viewport");
    }
    return *native_;
}

MapPoint Viewport::MapToScreen(MapPoint mapPoint) {
    return Detail::ToManaged(Native().mapToScreen(Detail::ToNative(mapPoint)));
}

MapPoint Viewport::ScreenToMap(MapPoint screenPoint) {
    return Detail::ToManaged(Native().screenToMap(Detail::ToNative(screenPoint)));
}

Envelope Viewport::MapExtent::get() {
    return Detail::ToManaged(Native().mapExtent());
}

ScreenSize Viewport::Size::get() {
    return Detail::ToManaged(Native().screenSize());
}

double Viewport::Scale::get() {
    return Native().scale();
}

}  // namespace Interop
}  // namespace Cartograph
