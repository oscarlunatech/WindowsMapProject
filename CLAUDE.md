# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Cartograph: a fast, correct desktop GIS for Windows (C++ core, eventual WPF shell).

**This file is the plan of record.** The phase plan below is authoritative — [README.md](README.md) summarizes it for human readers and holds the user-facing material (design goals, build, CLI usage, style-file format); it does not restate the per-phase detail. [DECISIONS.md](DECISIONS.md) is the real-time log of *why* choices were made, [BENCHMARKS.md](BENCHMARKS.md) the perf numbers with dates and hardware. Read DECISIONS before making an architectural choice — most surprising things in this codebase are surprising on purpose and the reason is written down.

## Current state and the plan to 1.0

**Done: phases 1–8. Next: Phase 9 (multi-layer map model).**

Work happens in ordered phases, and **skipping ahead defeats the point** — each phase is buildable, tested and green before the next starts. If a phase looks like it needs something from a later one, that's a signal to re-scope with the user, not to quietly pull the later work forward.

**Scope changed after Phase 7** (2026-08-24): the target is now a complete GIS at 1.0, not the read-only viewer phases 1–7 were scoped against. Anything written before that date — a code comment, a DECISIONS entry — may describe a narrower project. See the scope-change entry in DECISIONS.md.

### Phases

| | Phase | |
|---|---|---|
| **1** | Data model — `Dataset`/`Layer`/`Feature`/`Geometry`, GDAL confined to `dataset.cpp` | done |
| **2** | Off-screen rendering — Direct2D + WIC to PNG, golden-image test | done |
| **3** | Live window — Win32 `HWND`, message loop, pan/zoom | done |
| **4** | Make it fast — R-tree culling, Douglas-Peucker simplification, batched draw calls | done |
| **5** | Threading — `jobs::ThreadPool`, background load, parallel per-layer draw prep | done |
| **6** | Reprojection — `crs::Transformer` over PROJ, every layer normalized to EPSG:4326 | done |
| **7** | Symbology — single/categorized/graduated renderers, JSON style files | done |
| **8** | Identify — `geom::predicates` + `query::identify`, `identify` CLI subcommand, click-to-identify in the viewer | done |
| **9** | **Multi-layer map model** — open N files, ordered layers, visibility/opacity. Retires "one `Dataset` per run"; `drawDataset*` becomes `drawMap` | next |
| **10** | Display CRS — user-selectable, on-the-fly reprojection, Web Mercator default. Revisits Phase 6 (see constraints below) | |
| **11** | Raster — GDAL raster read, georeferenced display, band selection + stretch. First non-vector data path in the codebase | |
| **12** | `Cartograph.Interop` — C++/CLI marshalling boundary, no logic | |
| **13** | WPF shell I — window, hosted D2D map surface, layer list, toolbar | |
| **14** | WPF shell II — identify panel, attribute table, selection, symbology editor | |
| **15** | Labels — DirectWrite, placement rules, collision avoidance. Deferred out of Phase 7 deliberately | |
| **16** | Mutable data model — feature mutation + cache invalidation. **Prerequisite for 17** (see constraints) | |
| **17** | Editing — digitize, move/delete vertices and features, snapping, undo/redo | |
| **18** | Persistence — write to GeoPackage/Shapefile, project save/load. Ends the read-only era | |
| **19** | Analysis toolbox — buffer, clip, intersect, dissolve, spatial join, validity. First actual use of GEOS, a dependency since Phase 1 | |
| **20** | Basemaps — XYZ/slippy tiles, async fetch, disk cache. Needs Phase 10's Web Mercator | |
| **21** | Export — print layout, PNG/PDF at arbitrary scale. Reuse `drawDatasetCulled`, don't grow `Renderer::render` a mode flag | |

### Two constraints that will bite if forgotten

1. **Phase 16 must precede Phase 17.** `style::Stylesheet` precomputes every feature's symbol at construction, and `LayerCache` precomputes an R-tree plus simplification buckets — both justified explicitly by features being immutable after `Dataset::open()` returns. Editing kills that justification. Phase 16 exists so the reckoning is a deliberate phase with its own tests, not something discovered midway through building digitizing tools.
2. **Phase 10 reopens Phase 6's central decision.** Phase 6 normalizes every layer to `EPSG:4326` at load, chosen partly because the golden-image fixture is already in it (keeping that test byte-exact). A selectable display CRS makes 4326 one option among many. Keeping it explicitly available is what should preserve the golden-image test — verify that at the *start* of Phase 10, not the end.

### How thin each new pillar's 1.0 slice is

All four pillars are in, but each is scoped to a genuine-but-minimal slice so "fully functional" stays checkable. Don't widen these without re-scoping with the user first.

| Pillar | In 1.0 | Not in 1.0 |
|---|---|---|
| Raster | Display, georeferencing, band selection, contrast stretch | Band math, reclassify, raster analysis, terrain derivatives |
| Editing | Geometry + attribute editing, snapping, undo/redo, save | Topology editing, multi-user locking, versioning |
| Analysis | Core GEOS overlay/proximity ops, one at a time | Graphical modeler, batch processing, network analysis |
| Web | XYZ/slippy tile basemaps | WMS, WFS, WMTS, authenticated services |

Permanently out of scope regardless: cross-platform, 3D scenes/terrain, network analysis, mobile/web clients, plugin or scripting host.

### When a phase completes

Update this file (move the phase to `done`, advance the "Next:" line above), add a DECISIONS.md entry for any real fork in the road, add BENCHMARKS.md numbers if the phase touched performance, and update README's status line. There is **no CI** — the Phase 1 DECISIONS entry anticipated it and it still doesn't exist, so a green local `ctest --preset x64-debug` is currently the only gate.

## Build environment

- Toolchain: Visual Studio 2026 ("Visual Studio 18") with the Desktop C++ workload, standalone CMake 3.25+, standalone Ninja, vcpkg installed at `C:\dev\vcpkg` with `VCPKG_ROOT` set at the user env level.
- **Configuring/building must run from a Developer PowerShell for VS** (or with `vcvars64.bat` sourced first) — the Ninja generator needs `cl.exe` on `PATH`, which a plain shell doesn't have. If invoking from a fresh non-interactive shell (as Claude Code's Bash/PowerShell tools are), import the VS dev environment first:
  ```powershell
  $vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
  $tmp = [System.IO.Path]::GetTempFileName()
  cmd /c "`"$vcvars`" && set" > $tmp
  Get-Content $tmp | ForEach-Object { if ($_ -match "^([^=]+)=(.*)$") { [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process") } }
  Remove-Item $tmp
  ```
  Also set `$env:VCPKG_ROOT = "C:\dev\vcpkg"` and refresh `$env:Path` from the machine/user PATH first, since neither persists into a fresh shell invocation automatically.
- First configure builds every vcpkg dependency from source (GDAL, GEOS, PROJ, boost-geometry, Catch2) and takes tens of minutes; it's cached after that.

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --preset x64-debug          # or run the test exe directly, see below
```

Single test / filtered run (Catch2 tags, e.g. `[render]`, `[dataset]`, `[geometry]`, `[viewport]`, `[index]`, `[simplify]`, `[layer_cache]`, `[style]`, `[predicates]`, `[identify]`):
```
build\x64-debug\Cartograph.Core\tests\cartograph_core_tests.exe [render]
build\x64-debug\Cartograph.Core\tests\cartograph_core_tests.exe "Envelope expands to cover points"
```

Run the CLI: `build\x64-debug\cartograph_cli.exe {info|dump|identify|render|view|bench} <path> ...` — see README's Usage section for flags.

**Benchmark (and, for real perf-sensitive interactive use, run `view`) via the `x64-release` preset**, not `x64-debug`: `cmake --preset x64-release && cmake --build --preset x64-release`, binaries land in `build\x64-release\`. Since Phase 5 (threading), Debug numbers no longer reflect real per-frame cost — a naive thread pool's per-task synchronization overhead (heap-allocated tasks, mutex/condvar) is disproportionate to the work in an unoptimized (`/Od`, `/RTC1`, debug CRT) build; see the Phase 5 `DECISIONS.md`/`BENCHMARKS.md` entries. `x64-debug` remains correct for day-to-day development and is what the test suite above should keep running against.

## Architecture

**Two-target split, enforced deliberately:**
- `Cartograph.Core` (static lib, `Cartograph.Core/`) — all GDAL/OGR/GEOS/PROJ/Direct2D/WIC usage lives here. Must compile and be fully testable with no window, no HWND, no message loop. This is why Phases 1–2 (data model, off-screen rendering) were buildable and testable before Phase 3 (live window) existed at all.
- `cartograph_cli` (executable, `src/`) — the only place `HWND`, `WNDCLASSEXW`, and the Win32 message loop appear (`src/viewer.h/.cpp`). Win32 UI code must never leak into `Cartograph.Core`.

**Data flow through Core**, in the order a new dataset moves through the code:
1. `Dataset::open()` (`Cartograph.Core/src/dataset.cpp`) is the *only* file that includes GDAL/OGR headers. It converts every `OGRFeature`/`OGRGeometry` into Core's own `Feature`/`Geometry` on ingest and closes the GDAL dataset before returning — no OGR pointer ever survives past this call. Throws `DatasetOpenError` (not a null-return) on failure. Every layer is also reprojected to a common target CRS (`EPSG:4326`) during this conversion, via `crs::Transformer` (see item 7) — so layers from different source CRSs render correctly aligned rather than each staying in its own native coordinate space.
2. `Geometry` (`include/cartograph/geometry.h`) is intentionally data-oriented, not a class hierarchy: `parts()` returns `vector<Part>`, where a `Part` is `vector<Ring>` (exterior + holes for a polygon, one ring for a line/point). This shape is what both GEOS interop (future) and D2D path-building (`renderer.cpp`) want directly.
3. `render::Viewport` (`render/viewport.h`) does the Y-flipped map↔screen transform with a uniform (non-distorting) scale. It's an immutable value constructed fresh from `(mapExtent, screenSize)` whenever either changes — `Viewer` (the live window) doesn't mutate a Viewport, it rebuilds one every frame from its own mutable `mapExtent_`. Rather than letterboxing/pillarboxing when the window's aspect ratio doesn't match `mapExtent`'s, the constructor grows `mapExtent` about its own center along whichever axis is narrower, so the map always fills the whole screen — `Viewport::mapExtent()` therefore returns the *displayed* extent, which can be wider or taller than what was passed in.
4. `render::drawDataset(ID2D1RenderTarget&, ID2D1Factory&, ...)` (`render/renderer.h`) is the unculled, unbatched drawing entry point — `Renderer::render` (off-screen, WIC-backed, forces `D2D1_RENDER_TARGET_TYPE_SOFTWARE` for deterministic golden-image tests) calls it and is *never* touched by performance work, on purpose (see the Phase 4 DECISIONS.md entry) — that keeps the golden-image test permanently unaffected by anything downstream.
5. `render::drawDatasetCulled(...)` is the fast path: `Viewer::onPaint` (live) and `bench --culled` both call it with a `vector<render::LayerCache>` (one per `dataset.layers()`, built once at startup/before the timed loop), a `jobs::ThreadPool&`, and a `style::Stylesheet&` (see item 8). Each `LayerCache` bundles an `index::SpatialIndex` (bulk-loaded `boost::geometry::index::rtree`, hidden behind a pimpl so boost never appears in a public header) for viewport culling, plus `geom::simplify`'d geometry precomputed at a handful of zoom-level tolerance buckets. `drawDatasetCulled` also batches visible lines/polygons into one `ID2D1PathGeometry` and one draw call each, instead of one per feature — since Phase 7 that batching is per *(layer, symbol)* rather than per layer, since a categorized layer's features no longer all draw the same; a single-symbol layer collapses back to one batch and is identical to Phase 4's behavior. See `BENCHMARKS.md` for the before/after numbers this produced on the real NJ TIGER roads dataset (`scripts/fetch-data.ps1`, gitignored `data/`), including the Phase 7 measurement showing per-symbol batching costs nothing.
6. `jobs::ThreadPool` (`Cartograph.Core/include/cartograph/jobs/thread_pool.h`, added Phase 5) is a small fixed-size mutex/condvar pool shared by two things: `drawDatasetCulled` chunks each frame's per-layer query/simplify/coordinate-transform/`ID2D1PathGeometry`-construction work across it (only the final `DrawGeometry`/`FillGeometry`/`FillEllipse` submission stays serial on the caller's thread — both `ID2D1Factory`s in this codebase are `D2D1_FACTORY_TYPE_MULTI_THREADED`, which is what makes concurrent geometry construction across pool workers safe; `ID2D1RenderTarget` itself stays thread-affine regardless of factory type), and `Viewer` uses its own pool instance for a one-off background load (`Dataset::open` + `render::buildLayerCachesParallel`) on a dedicated `loaderThread_`, so the window appears before the dataset finishes loading. See the Phase 5 `DECISIONS.md` entries for the no-nested-submission rule this depends on, for a real `windows.h` `min`/`max` macro trap hit while wiring this in (any new `std::min`/`std::max` call reachable from a `windows.h`-including translation unit needs `(std::min)(...)`/`(std::max)(...)` parenthesization), and for why the factory type changed from `SINGLE_THREADED` after CPU profiling showed the coordinate-math-only parallelization left the actual geometry construction as the real bottleneck.
7. `crs::Transformer` (`Cartograph.Core/include/cartograph/crs/transformer.h`, added Phase 6) wraps PROJ's C API directly (hidden behind a pimpl, same pattern as `index::SpatialIndex` hiding boost) to reproject points between CRSs — "never write your own datum math," per the Phase 3 `DECISIONS.md` entry. `Dataset::open()` builds one per layer (see item 1); `isIdentity()` short-circuits to a passthrough when source and target are equivalent CRSs, which is what keeps already-`EPSG:4326` data (e.g. the golden-image fixture) bit-for-bit unchanged. See the Phase 6 `DECISIONS.md` entry for three traps hit while wiring this in: feed PROJ WKT2 (not GDAL's default WKT1 pretty-print, which can be ESRI-flavored and PROJ's own parser can reject), always call `proj_normalize_for_visualization` (fixes a silent axis-order swap for CRSs whose authority-defined order isn't conventional GIS lon/lat), and a missing NADCON/NTv2 datum-shift grid (this vcpkg PROJ install ships none) degrades a transform to identity for that step rather than failing loudly — accepted for the specific CRS pairs this project's own datasets use, revisit if that changes.

8. `style::Stylesheet` (`Cartograph.Core/include/cartograph/style/`, added Phase 7) is the symbology system. Three headers: `symbol.h` (`Color`/`Symbol` value types — one `Symbol` carries polygon fill+outline, line stroke and point fill/radius together, because the pre-Phase-7 hardcoded styling used different colors per geometry class and the renderer batches those separately anyway), `style_spec.h` (the dataset-independent `StyleSpec` parsed from a JSON style file, holding one `LayerStyle` = `variant<SingleSymbol, Categorized, Graduated>` per layer name, plus `StyleError`), and `stylesheet.h` (a `StyleSpec` bound to a concrete `Dataset`). **`Symbol`'s default member initializers are exactly the values `renderer.cpp` used to hardcode**, which is what makes `Stylesheet::defaults()` — and therefore the golden-image test — byte-exact; don't change them without re-baselining. `Stylesheet`'s constructor resolves *every feature's* symbol up front into a flat `[layer][feature] -> symbol index` table and dedupes symbols by value, so per-frame symbology costs one array index (same "precompute once, look up every frame" bargain as `LayerCache`, and deliberately a separate object from it so restyling doesn't invalidate the R-tree). That's why `drawDatasetCulled` takes it as a required parameter — build it once when the dataset loads, like the `LayerCache`s. `drawDataset`/`Renderer::render` are the one-shot paths and have defaulting overloads. JSON parsing lives in `src/style/style_io.cpp` with `nlohmann-json` linked `PRIVATE`; unknown layer/field/symbol keys throw rather than falling back silently. See README's Styling section for the file format and the Phase 7 `DECISIONS.md` entry for the rationale.

9. `geom::predicates` + `query::identify` (added Phase 8) are the hit-testing path. `geom::distanceTo(geometry, point)` (`geom/predicates.h`) is the single primitive: distance in map units, exactly `0` when the point is inside a polygon (holes respected), infinity for empty/Unknown geometry so it can never register a hit. Hand-rolled ray-casting and point-to-segment math rather than GEOS or Boost.Geometry — same reasoning as the `geom::simplify` DECISIONS entry (easy to verify against hand-computed cases, unlike datum math), and it avoids both pulling GEOS forward from Phase 19 and converting every candidate `Geometry` into a foreign type per click. `query::identify(...)` (`query/identify.h`) narrows candidates with each `LayerCache`'s R-tree, then runs the exact `distanceTo` test on every candidate — **an overlapping bounding box means very little** (Norway's extent covers a lot of sea), so the index narrows but never decides. Results are ordered topmost-layer-first then nearest-first, matching what's visibly under the cursor. It deliberately tests the *original* geometry, not `LayerCache`'s zoom-simplified copy, so identify answers about the real data rather than about the current zoom's rounding.

**Error handling convention**: exceptions at system boundaries (file I/O, COM/D2D/WIC failures, CLI argument parsing), not error codes — `DatasetOpenError`, `RenderError` and `StyleError` all derive from `std::runtime_error`; `main()` catches `const std::exception&` once at the top rather than per exception type.

## Testing

Catch2, `Cartograph.Core/tests/`. Two things worth knowing before adding tests:
- `test_render.cpp` does a **byte-exact** pixel comparison against a committed golden PNG (`tests/fixtures/golden/countries_world.png`), relying on the forced software rasterizer for determinism. See the DECISIONS.md entry on this — it's an accepted risk (unverified across machines/CI), not an oversight, so don't "fix" apparent flakiness here by loosening it without updating that entry. Its second test covers `drawDatasetCulled` (which the golden image doesn't touch) by counting exact-colored pixels rather than comparing whole images — that's the only coverage of symbology surviving the fast path's per-symbol batching.
- Fixture data (`tests/fixtures/ne_110m_admin_0_countries.*`) is Natural Earth 110m data, small enough to commit directly (per README's test-data tier table). Larger benchmarking datasets must never be committed: `scripts/fetch-data.ps1` downloads NJ TIGER/Line roads (21 counties) into gitignored `data/nj-roads/` — rerun it rather than expecting that data to already be present on a fresh clone.

