#pragma once

#include <exception>

#include "cartograph/crs/transformer.h"
#include "cartograph/dataset.h"
#include "cartograph/raster/raster_source.h"
#include "cartograph/style/style_spec.h"

#include "conversions.h"
// NOT cartograph/render/renderer.h - it includes <wrl/client.h>, which the
// Windows SDK #errors out under /clr. render_bridge.h re-declares the one
// thing this header needs from it. See render_bridge.h for the full story.
#include "render_bridge.h"

namespace Cartograph {
namespace Interop {

// Core's error-handling convention is exceptions at system boundaries, all
// deriving from std::runtime_error (see CLAUDE.md). A native exception must
// never be allowed to unwind out of managed code, so **every public entry
// point in this assembly translates**. That translation is the single most
// load-bearing thing this assembly does: get it wrong anywhere and a bad file
// path takes the whole shell down instead of showing a message box.
//
// The hierarchy mirrors Core's one-for-one rather than collapsing into a
// single type, because the shell has to tell these apart to react usefully -
// "that file isn't a dataset" is a different dialog from "your style file has
// a typo in it" - and matching on exception type is how a .NET caller expects
// to do that, not by parsing message strings.
public ref class CartographException : System::Exception {
public:
    CartographException(System::String ^ message) : System::Exception(message) {}
};

// cartograph::DatasetOpenError
public ref class DatasetOpenException : CartographException {
public:
    DatasetOpenException(System::String ^ message) : CartographException(message) {}
};

// cartograph::render::RenderError, which reaches here as
// cartograph_bridge::RenderFailure
public ref class RenderException : CartographException {
public:
    RenderException(System::String ^ message) : CartographException(message) {}
};

// cartograph::style::StyleError
public ref class StyleException : CartographException {
public:
    StyleException(System::String ^ message) : CartographException(message) {}
};

// cartograph::raster::RasterError
public ref class RasterException : CartographException {
public:
    RasterException(System::String ^ message) : CartographException(message) {}
};

// cartograph::crs::CrsError
public ref class CrsException : CartographException {
public:
    CrsException(System::String ^ message) : CartographException(message) {}
};

}  // namespace Interop
}  // namespace Cartograph

// Wraps a body of native calls so anything Core throws arrives managed.
//
// Spelled out as an explicit catch chain rather than routed through a shared
// `[[noreturn]] Translate()` helper: this way the compiler can see that every
// path either returns a value or throws, so a guarded function needs no
// unreachable `return {}` after it to compile.
//
// Order matters - the std::exception catch is last, and is the net that stops
// anything Core grows later (or anything the standard library throws, such as
// std::bad_alloc) from escaping untranslated.
#define CARTOGRAPH_TRY try {

#define CARTOGRAPH_CATCH                                                                       \
    }                                                                                          \
    catch (const ::cartograph::DatasetOpenError& e) {                                          \
        throw gcnew ::Cartograph::Interop::DatasetOpenException(                               \
            ::Cartograph::Interop::Detail::ToManaged(e.what()));                               \
    }                                                                                          \
    catch (const ::cartograph_bridge::RenderFailure& e) {                                      \
        throw gcnew ::Cartograph::Interop::RenderException(                                    \
            ::Cartograph::Interop::Detail::ToManaged(e.what()));                               \
    }                                                                                          \
    catch (const ::cartograph::style::StyleError& e) {                                         \
        throw gcnew ::Cartograph::Interop::StyleException(                                     \
            ::Cartograph::Interop::Detail::ToManaged(e.what()));                               \
    }                                                                                          \
    catch (const ::cartograph::raster::RasterError& e) {                                       \
        throw gcnew ::Cartograph::Interop::RasterException(                                    \
            ::Cartograph::Interop::Detail::ToManaged(e.what()));                                \
    }                                                                                          \
    catch (const ::cartograph::crs::CrsError& e) {                                             \
        throw gcnew ::Cartograph::Interop::CrsException(                                       \
            ::Cartograph::Interop::Detail::ToManaged(e.what()));                               \
    }                                                                                          \
    catch (const std::exception& e) {                                                          \
        throw gcnew ::Cartograph::Interop::CartographException(                                \
            ::Cartograph::Interop::Detail::ToManaged(e.what()));                               \
    }
