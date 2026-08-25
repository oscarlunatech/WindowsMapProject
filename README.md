# Cartograph

A narrow, fast, correct desktop GIS viewer for Windows. C++ core, WPF shell.

Status: **Phase 7 (symbology) complete.** See [DECISIONS.md](DECISIONS.md) and [BENCHMARKS.md](BENCHMARKS.md) for the running log.

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

`--style` takes a JSON style file (see below). Without it, every feature draws with the built-in default symbology.

## Styling

A style file maps layer names to a renderer. Three renderer types, matching the standard GIS trio:

```json
{
  "layers": {
    "countries": {
      "type": "graduated",
      "field": "POP_EST",
      "breaks": [
        { "max": 1000000,  "symbol": { "fill": "#f7fbff" } },
        { "max": 10000000, "symbol": { "fill": "#6baed6" } }
      ],
      "fallback": { "fill": "#08306b" }
    },
    "roads": {
      "type": "categorized",
      "field": "RTTYP",
      "categories": [
        { "value": "I", "symbol": { "line": "#e04a2f", "lineWidth": 2.5 } },
        { "value": "S", "symbol": { "line": "#f2a33c", "lineWidth": 1.8 } }
      ],
      "fallback": { "line": "#9aa0a6", "lineWidth": 0.8 }
    }
  },
  "default": { "type": "single", "symbol": { "fill": "#cccccc" } }
}
```

- **`single`** — one symbol for every feature in the layer. `"type"` may be omitted; it's the default.
- **`categorized`** — exact match on `field`, first matching category wins, `fallback` for the rest. Numbers compare numerically across OGR's Integer/Integer64/Real, so `3` matches whichever the driver produced.
- **`graduated`** — `field` classified into ascending `breaks`; a value ≤ a break's `max` selects it. Values above the last break, nulls, and non-numerics take `fallback`.

`default` applies to any layer without its own entry — useful for a multi-layer dataset like the 21-county TIGER roads set. A layer or field name the dataset doesn't have is an error, not a silent fallback.

Symbol keys (all optional; anything unnamed keeps its built-in default):

| Key | Applies to | Default |
|---|---|---|
| `fill` | polygon interiors | `#d9d9d9` |
| `outline` / `outlineWidth` | polygon boundaries | `#000000`, `1.0` |
| `line` / `lineWidth` | lines | `#0033cc`, `1.5` |
| `point` / `pointRadius` | points | `#ff0000`, `3.0` |

Colors are `#rgb`, `#rrggbb` or `#rrggbbaa`. A width or radius of `0` draws nothing of that class — that's how you turn polygon outlines off. [`styles/countries-by-continent.json`](styles/countries-by-continent.json) is a worked example against the committed test fixture:

```
cartograph_cli render Cartograph.Core/tests/fixtures/ne_110m_admin_0_countries.shp \
    --style styles/countries-by-continent.json -o continents.png
```

## Phases

Built in order; each phase is buildable and tested before the next starts.

| Phase | | Status |
|---|---|---|
| 1 | Data model — `Dataset`/`Layer`/`Feature`/`Geometry`, GDAL confined to `dataset.cpp` | done |
| 2 | Off-screen rendering — Direct2D + WIC to PNG, golden-image test | done |
| 3 | Live window — Win32 `HWND`, message loop, pan/zoom | done |
| 4 | Make it fast — R-tree culling, Douglas-Peucker simplification, batched draw calls | done |
| 5 | Threading — `jobs::ThreadPool`, background load, parallel per-layer draw prep | done |
| 6 | Reprojection — `crs::Transformer` over PROJ, every layer normalized to EPSG:4326 | done |
| 7 | Symbology — single/categorized/graduated renderers, JSON style files | done |
| 8 | Identify — click a feature, show its attributes | next |
| 9 | WPF shell — `Cartograph.Interop` (C++/CLI), layer list, toolbar, identify panel | |

## Test data

| Tier | What | Where |
|---|---|---|
| Fixture | Natural Earth 110m countries (~177 features) | committed, `Cartograph.Core/tests/fixtures/` |
| Benchmark | NJ TIGER/Line roads, 21 counties (189,314 features) | gitignored, `scripts/fetch-data.ps1` → `data/nj-roads/` |

Fixture data is small enough to commit and is what the golden-image test compares against. Benchmark data is never committed — rerun the fetch script on a fresh clone.

## Explicitly out of scope

- Geoprocessing toolbox, network analysis, 3D scenes
- Web services (WMS/WFS), tile servers, ArcGIS Online anything
- Cross-platform support — this is a Windows project on purpose
- Format writing — read-only until the editing stretch goal, if ever
