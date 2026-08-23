# Cartograph

A narrow, fast, correct desktop GIS viewer for Windows. C++ core, WPF shell.

Status: **Phase 4 (make it fast) complete.** Phase 5 (threading) is next. See [DECISIONS.md](DECISIONS.md) and [BENCHMARKS.md](BENCHMARKS.md) for the running log.

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
ctest --preset x64-debug
```

## Usage

```
cartograph_cli info <path>
cartograph_cli dump <path> [--limit N]
cartograph_cli render <path> [--bbox minX,minY,maxX,maxY] [--size WxH] -o <output.png>
cartograph_cli view <path>
cartograph_cli bench <path> [--frames N] [--culled] [-o results.csv]
```

`info`/`dump` print layer metadata and feature attributes. `render` draws a dataset to a PNG with no window (used for the golden-image tests). `view` opens a live window: left-drag to pan, scroll wheel to zoom under the cursor, arrow keys to pan, `+`/`-` to zoom. `bench` times a fixed camera path over a dataset and writes per-frame ms to a CSV — pass `--culled` to exercise the indexed/simplified/batched draw path instead of the naive one (see BENCHMARKS.md).

## Explicitly out of scope

- Geoprocessing toolbox, network analysis, 3D scenes
- Web services (WMS/WFS), tile servers, ArcGIS Online anything
- Cross-platform support — this is a Windows project on purpose
- Format writing — read-only until the editing stretch goal, if ever
