# Decisions

Short entries: what was chosen, what was rejected, why. Add one whenever a real fork in the road gets picked.

## 2026-08-23 — vcpkg in manifest mode, not vendored

Chose a local vcpkg install (`VCPKG_ROOT`) with `builtin-baseline` pinned in `vcpkg.json`, over vendoring vcpkg as a git submodule.

Would revisit if reproducibility across machines becomes a problem — the baseline pin should be enough, but a submodule guarantees the tool version too.

## 2026-08-23 — Ninja generator, requires a Developer shell

`CMakePresets.json` uses the Ninja generator rather than the Visual Studio generator, for fast incremental builds and CI parity (GitHub's `windows-latest` runners ship Ninja). The tradeoff: Ninja can't locate `cl.exe` on its own, so configuring/building must happen from a Developer Command Prompt/PowerShell (or after running `vcvars64.bat`) rather than a plain shell. CI will need `ilammy/msvc-dev-cmd` (or equivalent) before the CMake steps.

## 2026-08-23 — proj.db copied next to the executable, path set at runtime

PROJ doesn't discover `proj.db` next to the exe on Windows by default, and vcpkg's applocal DLL-copy step doesn't cover data files. Fixed by copying `proj.db` alongside the binary in a CMake post-build step, and pointing `PROJ_DATA` at the executable's own directory at startup (`CPLGetExecPath` + `CPLSetConfigOption`) — so it works with no environment setup on a clean machine, rather than relying on a `PROJ_DATA`/`PROJ_LIB` env var the user would have to remember to set.

## 2026-08-23 — golden-image test compares PNGs byte-for-byte, not with a tolerance

`test_render.cpp` requires the freshly rendered bitmap to exactly match the committed golden PNG, no per-pixel tolerance. This only works because rendering forces `D2D1_RENDER_TARGET_TYPE_SOFTWARE` (WARP), which should be deterministic regardless of the local GPU/driver.

Risk: this session only has one machine to test on, so cross-machine/CI determinism (different Windows or D2D versions producing slightly different antialiasing) is unverified. Chose exact matching anyway rather than building a tolerance-based comparison up front for a flakiness problem that hasn't actually been observed yet. Revisit with a per-pixel-tolerance + mismatch-percentage comparison if CI proves the exact match too brittle.

## 2026-08-23 — hand-rolled Douglas-Peucker instead of GEOS

`geom::simplify` (`Cartograph.Core/src/geom/simplify.cpp`) implements Ramer-Douglas-Peucker directly rather than calling GEOS (`geos::simplify::DouglasPeuckerSimplifier`), even though GEOS is already a dependency and is the project's designated geometry-algorithms library.

Douglas-Peucker is a simple, well-defined recursive algorithm over a flat point list, unit-tested directly against hand-computed expected output. That's a different category from the "never write your own datum math" rule for reprojection (Phase 6) — that rule is about subtle, hard-to-verify correctness (datum shifts, edge-of-domain behavior) where a wrong answer can look plausible. A wrong Douglas-Peucker implementation is easy to catch with a handful of known-distance test cases, which `test_simplify.cpp` does. Would revisit if the outline's later geometry work (buffer, intersect, validity) needs GEOS integration anyway, at which point routing simplification through GEOS too might reduce total surface area rather than add it.

## 2026-08-23 — Viewport fills the window by expanding the extent, not letterboxing

`render::Viewport` used to preserve aspect ratio by uniform-scaling to the limiting axis and centering the result, leaving blank letterbox/pillarbox bars whenever the window's aspect ratio didn't match `mapExtent`'s. Changed it to instead grow `mapExtent` about its own center along whichever axis is narrower, so the map always fills the entire screen at the same uniform scale.

Two alternatives considered and rejected:
- **Crop ("cover")**: scale to the *larger* of scaleX/scaleY and let the render target clip the overflow. Fills the window with no distortion, but permanently hides map content at the edges on whichever axis is over-full — bad for a viewer whose job is showing data.
- **Stretch (non-uniform scale)**: independent X/Y scale factors so the map exactly fills the window with zero blank space. Rejected because it visibly distorts geometry (a square county renders as a rectangle), which conflicts with the "correct before fast" design goal — this is a geometric distortion, not just a display convenience.

Expanding the extent was chosen because it never hides or distorts data, matches the resize behavior of existing desktop GIS viewers (QGIS, ArcMap), and is a small, self-contained change in `Viewport`'s constructor — `Viewer` still just rebuilds a `Viewport` fresh every frame from its own `mapExtent_`, unchanged. `Viewport::mapExtent()` now returns the displayed extent rather than the exact one passed in, which callers should be aware of. The golden-image test (`test_render.cpp`) was unaffected since its fixture's bbox and screen size already share an exact 2:1 aspect ratio, so the expansion is a no-op there.

## 2026-08-23 — Phase 4 optimizations only touch the live Viewer, not Renderer::render

`SpatialIndex`/`LayerCache`/`drawDatasetCulled` are wired into `Viewer` (`src/viewer.cpp`) and the `bench` CLI subcommand, but `Renderer::render` (the off-screen, golden-image-tested path) still calls the original, unculled `drawDataset` unchanged.

Deliberate: the performance problem Phase 4 solves is specifically live pan/zoom frame time, not one-shot file rendering, and leaving `Renderer::render` untouched meant the Phase 2 golden-image test needed zero changes or re-baselining through this entire phase (verified: `test_render.cpp` passed unmodified before, during, and after all Phase 4 work). If a future phase wants a culled/batched off-screen render path (e.g. for faster large-area exports), it should reuse `drawDatasetCulled` directly rather than growing `Renderer::render` a mode flag.
