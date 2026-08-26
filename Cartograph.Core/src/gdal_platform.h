#pragma once

// Internal to Cartograph.Core - deliberately not under include/, since nothing
// outside the library should need it.
//
// Shared by the two translation units that talk to GDAL: dataset.cpp (vector)
// and raster/raster_source.cpp (raster). Either can be the first thing a
// process touches, so the setup has to be callable from both rather than
// living in whichever one happened to run first.

namespace cartograph::detail {

// Registers GDAL's drivers and points PROJ at the proj.db that ships beside
// the executable (see CMakeLists.txt - PROJ doesn't find it there on its own).
//
// Idempotent and thread-safe: uses std::call_once rather than a plain static
// bool, which matters because Dataset::open and RasterSource both get called
// from background loader threads and a double-checked bool has no memory
// barrier.
void ensureGdalPlatformSetup();

}  // namespace cartograph::detail
