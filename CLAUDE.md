# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Cartograph: a narrow, fast, correct desktop GIS viewer for Windows (C++ core, eventual WPF shell). Full design rationale, phase-by-phase plan, and explicit non-goals live in [README.md](README.md); real-time decision log in [DECISIONS.md](DECISIONS.md); perf numbers in [BENCHMARKS.md](BENCHMARKS.md). Read all three before making architectural choices — this project is being built in ordered phases (see README's Phases section) and skipping ahead defeats the point.

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

Single test / filtered run (Catch2 tags, e.g. `[render]`, `[dataset]`, `[geometry]`, `[viewport]`):
```
build\x64-debug\Cartograph.Core\tests\cartograph_core_tests.exe [render]
build\x64-debug\Cartograph.Core\tests\cartograph_core_tests.exe "Envelope expands to cover points"
```

Run the CLI: `build\x64-debug\cartograph_cli.exe {info|dump|render|view} <path> ...` — see README's Usage section for flags.

## Architecture

**Two-target split, enforced deliberately:**
- `Cartograph.Core` (static lib, `Cartograph.Core/`) — all GDAL/OGR/GEOS/PROJ/Direct2D/WIC usage lives here. Must compile and be fully testable with no window, no HWND, no message loop. This is why Phases 1–2 (data model, off-screen rendering) were buildable and testable before Phase 3 (live window) existed at all.
- `cartograph_cli` (executable, `src/`) — the only place `HWND`, `WNDCLASSEXW`, and the Win32 message loop appear (`src/viewer.h/.cpp`). Win32 UI code must never leak into `Cartograph.Core`.

**Data flow through Core**, in the order a new dataset moves through the code:
1. `Dataset::open()` (`Cartograph.Core/src/dataset.cpp`) is the *only* file that includes GDAL/OGR headers. It converts every `OGRFeature`/`OGRGeometry` into Core's own `Feature`/`Geometry` on ingest and closes the GDAL dataset before returning — no OGR pointer ever survives past this call. Throws `DatasetOpenError` (not a null-return) on failure.
2. `Geometry` (`include/cartograph/geometry.h`) is intentionally data-oriented, not a class hierarchy: `parts()` returns `vector<Part>`, where a `Part` is `vector<Ring>` (exterior + holes for a polygon, one ring for a line/point). This shape is what both GEOS interop (future) and D2D path-building (`renderer.cpp`) want directly.
3. `render::Viewport` (`render/viewport.h`) does the aspect-ratio-preserving, Y-flipped map↔screen transform. It's an immutable value constructed fresh from `(mapExtent, screenSize)` whenever either changes — `Viewer` (the live window) doesn't mutate a Viewport, it rebuilds one every frame from its own mutable `mapExtent_`.
4. `render::drawDataset(ID2D1RenderTarget&, ID2D1Factory&, ...)` (`render/renderer.h`) is the single shared drawing entry point — both `Renderer::render` (off-screen, WIC-backed, forces `D2D1_RENDER_TARGET_TYPE_SOFTWARE` for deterministic golden-image tests) and `Viewer::onPaint` (live, hardware-backed `HwndRenderTarget`, for a realistic frame-time baseline) call into it, so brush/path-building logic exists exactly once.

**Error handling convention**: exceptions at system boundaries (file I/O, COM/D2D/WIC failures, CLI argument parsing), not error codes — `DatasetOpenError` and `RenderError` both derive from `std::runtime_error`; `main()` catches `const std::exception&` once at the top rather than per exception type.

**Styling is currently hardcoded** in `renderer.cpp` (gray polygon fill, blue lines, red points) — there is no symbology system yet by design; that's Phase 7.

## Testing

Catch2, `Cartograph.Core/tests/`. Two things worth knowing before adding tests:
- `test_render.cpp` does a **byte-exact** pixel comparison against a committed golden PNG (`tests/fixtures/golden/countries_world.png`), relying on the forced software rasterizer for determinism. See the DECISIONS.md entry on this — it's an accepted risk (unverified across machines/CI), not an oversight, so don't "fix" apparent flakiness here by loosening it without updating that entry.
- Fixture data (`tests/fixtures/ne_110m_admin_0_countries.*`) is Natural Earth 110m data, small enough to commit directly (per README's test-data tier table). Larger benchmarking datasets (TIGER roads, OSM extracts) must never be committed — they're fetched by a script, not checked in (that script doesn't exist yet; it lands whenever Phase 4 needs the large dataset).

## Git

Commit messages in this repo do not get a `Co-Authored-By: Claude` trailer — this was requested explicitly and applies to all commits here, not just the first few.
