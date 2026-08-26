# Cartograph

A fast, correct desktop GIS for Windows. C++ core, WPF shell.

Status: **Phase 11 (raster) complete** — 11 of 21 phases to 1.0. See [DECISIONS.md](DECISIONS.md) and [BENCHMARKS.md](BENCHMARKS.md) for the running log.

## Design goals

1. **Correct before fast.** A wrong map drawn at 120fps is worthless. Every projection and geometry path gets a test.
2. **Fast enough to feel native.** Target: pan and zoom a 500k-feature layer at 60fps on a mid-range laptop.
3. **The core knows nothing about the UI.** No Windows types, no WPF, no rendering API leak into the data and geometry layers.
4. **Complete, not comprehensive.** 1.0 covers a whole GIS workflow — open, project, style, label, identify, edit, analyze, export — with each capability actually finished. It does not try to match QGIS feature-for-feature. Depth where it counts, an honest "no" everywhere else.
5. **Every phase ships.** The build is green and the tests pass at the end of every phase. No long-lived broken branches, no phase that only makes sense once the next one lands.

## Architecture

Native core, managed shell, explicit boundary between them.

```
Cartograph.Shell    (C#, WPF, MVVM)          — layer list, toolbar, identify panel, attribute table
Cartograph.Interop  (C++/CLI)                — thin marshalling layer, no logic
Cartograph.Core     (C++20, static lib)      — data/, geom/, crs/, index/, render/, jobs/, style/
```

Anything in `Cartograph.Core` must compile and be testable without a window existing. Phases 1–7 were built and tested with no shell in existence at all, and phases 8–11 continue that way on purpose: the hard problems stay in testable Core, so the shell arrives as a view onto a model that already behaves.

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
cartograph_cli info     <path>...
cartograph_cli dump     <path>... [--limit N]
cartograph_cli layers   <path>... [--crs EPSG:NNNN]
cartograph_cli identify <path>... --at x,y [--crs EPSG:NNNN] [--tolerance N] [--limit N]
cartograph_cli render   <path>... [--bbox ...] [--size WxH] [--crs EPSG:NNNN] [--style s.json] -o <out.png>
cartograph_cli view     <path>... [--crs EPSG:NNNN] [--style s.json]
cartograph_cli bench    <path>... [--frames N] [--culled] [--crs EPSG:NNNN] [--style s.json] [-o out.csv]
```

**Multiple paths stack as layers**, first path at the bottom, and each file's layers keep their own order within the stack. So `view roads.shp parcels.shp` draws parcels over roads, and `layers` prints the resulting stack:

```
$ cartograph_cli layers a.shp b.shp
2 layer(s), drawn bottom to top:
  [0] tl_2023_34001_roads  features: 9345   source: a.shp
  [1] tl_2023_34003_roads  features: 14650  source: b.shp
```

`info`/`dump` print layer metadata and feature attributes; `layers` prints the map's layer stack in draw order. `identify` reports every feature within `--tolerance` map units of a point, topmost layer first — tolerance defaults to 0.1% of the dataset extent's diagonal, and `0` asks strictly "which polygons contain this point". `render` draws a dataset to a PNG with no window (used for the golden-image tests). `view` opens a live window: left-drag to pan, **left-click to identify**, scroll wheel to zoom under the cursor, arrow keys to pan, `+`/`-` to zoom. `bench` times a fixed camera path over a dataset and writes per-frame ms to a CSV — pass `--culled` to exercise the indexed/simplified/batched draw path instead of the naive one (see BENCHMARKS.md).

`--style` takes a JSON style file (see below). Without it, every feature draws with the built-in default symbology.

## Coordinate systems

Every layer is reprojected into one **display CRS** as it loads, so files in different source projections line up. `--crs` selects it (any PROJ-recognized string), defaulting to **EPSG:3857** — Web Mercator, what web maps and XYZ basemap tiles use.

`--bbox` and `--at` are read in the display CRS, so they're metres by default and degrees under `--crs EPSG:4326`:

```
cartograph_cli render world.shp --crs EPSG:4326 --bbox -180,-90,180,90 -o wgs84.png
cartograph_cli identify world.shp --crs EPSG:4326 --at 2.3,46.5
```

`bench` is the one exception: it defaults to `EPSG:4326` so its numbers stay comparable with everything already recorded in [BENCHMARKS.md](BENCHMARKS.md).

Coordinates are clamped to the target CRS's declared area of use. This matters more than it sounds: PROJ itself does not clamp, and latitude −90 in Web Mercator projects to y = −242,529,000 — twelve times the projection's own bound, and finite, so nothing errors. Without the clamp any dataset containing Antarctica would blow the map extent up 12× and render the world as a sliver. With it, Antarctica flattens onto the bottom edge, exactly as every web map shows it.

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

## Rasters

Raster files are opened exactly like vector ones — same commands, same layer stack, no flag saying which is which. So a shaded-relief basemap under vector borders is just two paths in the right order:

```
cartograph_cli view hillshade.tif borders.shp
```

Rasters are warped into the display CRS on open, so they line up with vector layers, and the visible window is **re-read at screen resolution whenever the view moves** — so they stay sharp at every zoom rather than blurring out of one load-time bitmap. In the live viewer that read happens on a background thread; the previous image keeps drawing, correctly positioned, until the new one lands. Clicking a raster reports the raw value of every band under the cursor.

A `raster` style controls bands and contrast:

```json
{
  "layers": {
    "landsat": {
      "type": "raster",
      "bands": [4, 3, 2],
      "stretch": "percentile",
      "stretchRange": [2, 98]
    }
  }
}
```

- **`bands`** — 1-based, either three (red, green, blue) or one (grayscale). Omit for automatic: three or more bands take the first three, a single band renders as gray.
- **`stretch`** — `auto` (default), `none`, `minmax` or `percentile`, with `stretchRange` setting the percentile cuts. Contrast is computed from the pixels actually on screen, so zooming into a dark corner brings out its detail.

`auto` asks the file what it is: an 8-bit image whose bands are tagged red/green/blue is already display-ready and is left alone, while anything else (a DEM, 16-bit satellite bands) gets a percentile stretch. That distinction is worth knowing about — percentile-stretching a finished RGB basemap visibly oversaturates it, and a DEM is invisible without a stretch, so no single default is right for both.

[`styles/natural-earth-basemap.json`](styles/natural-earth-basemap.json) is a worked example. Fetch the raster it expects with `scripts/fetch-raster.ps1`, then:

```
cartograph_cli view data/raster/NE1_50M_SR_W/NE1_50M_SR_W.tif \
    Cartograph.Core/tests/fixtures/ne_110m_admin_0_countries.shp \
    --style styles/natural-earth-basemap.json
```

## Roadmap

Built in ordered phases; each is buildable and tested before the next starts. **11 of 21 phases done.** The authoritative per-phase plan — scope, ordering constraints, and how thin each pillar's 1.0 slice is — lives in [CLAUDE.md](CLAUDE.md), so it stays in one place rather than drifting between two.

| Phases | | |
|---|---|---|
| 1–7 | **Foundations** — data model, rendering, live window, performance, threading, reprojection, symbology | done |
| 8–11 | **A real map model** — identify, multi-layer, display CRS, raster display | done |
| 12–14 | **The shell** — C++/CLI interop, then WPF: map surface, layer list, identify panel, attribute table | next |
| 15–18 | **Cartography and editing** — labels, a mutable data model, digitizing, persistence | |
| 19–21 | **Analysis and output** — GEOS toolbox, XYZ basemaps, print/export | |

The shell deliberately lands at 12, not 8: the model still has to grow multi-layer support, a selectable display CRS and a non-vector data path, and building a UI against a single-dataset, single-CRS, vector-only model means reworking it underneath itself three times. Phases 1–7 showed hard problems are cheap to get right in a Core with no window.

## Test data

| Tier | What | Where |
|---|---|---|
| Fixture | Natural Earth 110m countries (~177 features) | committed, `Cartograph.Core/tests/fixtures/` |
| Fixture | Synthetic GeoTIFFs for the raster tests | generated in-process, never written to the repo |
| Raster | Natural Earth 50m shaded relief (~167MB) | gitignored, `scripts/fetch-raster.ps1` → `data/raster/` |
| Benchmark | NJ TIGER/Line roads, 21 counties (189,314 features) | gitignored, `scripts/fetch-data.ps1` → `data/nj-roads/` |

Fixture data is small enough to commit and is what the golden-image test compares against. Benchmark data is never committed — rerun the fetch script on a fresh clone.

## Explicitly out of scope

Permanently:

- **Cross-platform support** — this is a Windows project on purpose, and the Direct2D/WPF choices are load-bearing, not incidental
- **3D scenes and terrain** — a different renderer and a different problem
- **Network analysis** (routing, service areas, isochrones)
- **Mobile or web clients**, ArcGIS Online / Enterprise integration
- **Plugin or scripting host** — no embedded Python, no extension API

Deferred past 1.0 rather than rejected — [CLAUDE.md](CLAUDE.md)'s pillar table has the full in/out breakdown:

- WMS/WFS/WMTS and authenticated remote services (XYZ tiles *are* in, at Phase 20)
- Raster analysis: band math, reclassification, terrain derivatives
- Graphical model builder and batch geoprocessing
- Topology editing, versioning, multi-user editing

The 1.0 target is a GIS that does a complete workflow well, not one that does everything shallowly. Anything above that turns out to matter more than something inside the plan gets argued out in [DECISIONS.md](DECISIONS.md) first, not smuggled into a phase.
