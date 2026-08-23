# Cartograph

A narrow, fast, correct desktop GIS viewer for Windows. C++ core, WPF shell.

Status: **Phase 0 — build environment.** Nothing runnable yet.

## Design goals

1. **Correct before fast.** A wrong map drawn at 120fps is worthless. Every projection and geometry path gets a test.
2. **Fast enough to feel native.** Target: pan and zoom a 500k-feature layer at 60fps on a mid-range laptop.
3. **The core knows nothing about the UI.** No Windows types, no WPF, no rendering API leak into the data and geometry layers.
4. **Narrow scope, no clone.** This is a viewer with identify and basic symbology. Not an editor, not a geoprocessing suite.

## Architecture

Native core, managed shell, explicit boundary between them.

```
Cartograph.Shell    (C#, WPF, MVVM)          — layer list, toolbar, identify panel
Cartograph.Interop  (C++/CLI)                — thin marshalling layer, no logic
Cartograph.Core     (C++20, static lib)      — data/, geom/, crs/, index/, render/, jobs/
```

Anything in `Cartograph.Core` must compile and be testable without a window existing.

## Building

Prerequisites: Visual Studio 2022+ with the "Desktop development with C++" workload, [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set, and CMake 3.25+ on PATH.

Configure and build from a **Developer PowerShell for VS** (Ninja needs `cl.exe` on PATH):

```
cmake --preset x64-debug
cmake --build --preset x64-debug
```

## Explicitly out of scope

- Geoprocessing toolbox, network analysis, 3D scenes
- Web services (WMS/WFS), tile servers, ArcGIS Online anything
- Cross-platform support — this is a Windows project on purpose
- Format writing — read-only until the editing stretch goal, if ever
