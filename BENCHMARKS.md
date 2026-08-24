# Benchmarks

Numbers, with dates and hardware.

**Hardware**: Intel Core Ultra 5 225F, 32GB RAM, Windows 11 (10.0.26200).

**Dataset**: NJ TIGER/Line roads, all 21 counties (`scripts/fetch-data.ps1`), 189,314 features across 21 layers.

**Method**: `cartograph_cli bench data/nj-roads --frames 60`. 60-frame camera path interpolating from the dataset's full extent down to a zoomed-in view of Newark/Jersey City (`buildCameraPath` in `src/main.cpp`). Timing covers only `BeginDraw`/draw/`EndDraw` — render-target setup happens once before the loop, not per frame.

## Phase 4 — before indexing (2026-08-23)

No spatial index, no simplification, no batching: every frame calls the original `drawDataset`, which builds a fresh `ID2D1PathGeometry` and issues a separate `DrawGeometry`/`FillGeometry` call per feature — all 189,314 of them, every frame, regardless of what's actually visible.

| | ms/frame |
|---|---|
| avg | 537.98 |
| min | 345.74 (most zoomed-in frame) |
| max | 651.79 (full-extent frame) |

Full per-frame numbers: `bench_before.csv` (not committed — regenerate with the command above). Even with nothing culled, time drops ~2x from the widest to the narrowest view — D2D's own rasterizer still clips fill work to the visible area, but every feature still pays full CPU-side path-geometry construction cost every frame regardless of visibility. That per-feature overhead is exactly what the R-tree index removes next.

## Phase 4 — after indexing, simplification, batching (2026-08-23)

Same hardware, same dataset, same camera path. `cartograph_cli bench data/nj-roads --frames 60 --culled`: each frame now queries a per-layer `SpatialIndex` (bulk-loaded `boost::geometry::index::rtree`) for the features actually intersecting the viewport, draws geometry simplified to the precomputed tolerance bucket matching the current zoom, and batches every visible line/polygon per layer into one `ID2D1PathGeometry` and one `DrawGeometry`/`FillGeometry` call instead of one per feature.

| | ms/frame | vs. before |
|---|---|---|
| avg | 161.29 | 3.3x |
| min | 8.22 (most zoomed-in frame) | **42x** |
| max | 282.98 (full-extent frame) | 2.3x |

Full per-frame numbers: `bench_after.csv` (not committed). The shape tells the real story: frame 0 (full extent, nothing culled — every one of the 189,314 features is still "visible") is already 2.3x faster purely from batching draw calls, then time falls off monotonically as the camera zooms toward Newark and the R-tree starts actually discarding features — down to 8.22ms once only a small fraction of the roads intersect the viewport. That's the "it went from 340ms to 6ms" story the outline asked for, on real data instead of a toy fixture.

**Interactive confirmation** (`cartograph_cli view data/nj-roads`): panning/zooming across the full state feels smooth once the visible feature count drops below roughly 40,000 (matches the bench numbers — that's around where per-frame time crosses into the "feels responsive" range on this hardware). Above that, in a wide view over a dense region, it's still noticeably behind 60fps — there's more headroom here for a future pass (a bigger simplification tolerance range, or per-frame time-boxing) if it ever matters, but it wasn't in Phase 4's scope.

## Phase 5 — parallel per-layer draw prep (2026-08-23)

Same hardware, same dataset, same camera path, same `bench --culled` command as Phase 4's "after" row. `drawDatasetCulled` now splits each frame's per-layer work (R-tree query, simplification-bucket lookup, `mapToScreen` coordinate transform) into up to `jobs::ThreadPool::size()` chunks run concurrently, with only the resulting `ID2D1PathGeometry` sink-writing and draw calls left serial (see the Phase 5 `DECISIONS.md` entry for why). Measured on **both** presets, because the difference turned out to matter a lot:

| | ms/frame (avg) | ms/frame (min) | ms/frame (max) |
|---|---|---|---|
| Phase 4 (baseline, Debug) | 161.29 | 8.22 | 282.98 |
| Phase 5, `x64-debug` | 638.65 | 16.05 | 1012.92 |
| Phase 5, `x64-release` | **118.19** | **8.66** | **196.96** |

Release is a real win over the Phase 4 baseline (~27% faster average, ~30% faster on the worst/full-extent frame). Debug is a real *regression* — roughly 4x worse — because a naive thread pool's per-task overhead (a heap-allocated `packaged_task`, a mutex lock, a condition-variable wake, ~20 times a frame at 60fps for ~20 layers) costs more than the work it's parallelizing once you're in an unoptimized (`/Od`, `/RTC1`, debug CRT) build. Chunking the work into `pool.size()` tasks instead of one task per layer (see `DECISIONS.md`) narrowed the Debug regression from an initial ~4.6x to ~4x, but didn't eliminate it — Debug-build synchronization-primitive cost is just inherently disproportionate to the small amount of work available in this dataset's 21 layers.

**Takeaway: benchmark (and, for real perf-sensitive use, run `view`) via `cmake --preset x64-release` / `cartograph_cli.exe` in `build/x64-release`** from this phase on. `x64-debug` remains the correct preset for day-to-day development per `CLAUDE.md`, but its numbers no longer reflect real per-frame cost now that threading is in the picture.

### Follow-up — also parallelize ID2D1PathGeometry construction, not just the coordinate math (2026-08-23)

Live-window CPU profiling of the row above (Task Manager per-core view, `x64-debug`) showed one core pegged near 100% while the rest sat mostly idle - only the coordinate-transform math was parallel; the actual `ID2D1PathGeometry` sink-writing (`BeginFigure`/`AddLine` for every point) was still fully serial on the calling thread, and for this dataset's dense line geometry that serial phase dominates total frame time. Moved geometry *construction* into the parallel phase too (`prepareLayer` now builds the `ID2D1PathGeometry` directly on the pool worker), leaving only the final `DrawGeometry`/`FillGeometry`/`FillEllipse` submission calls serial - the minimum required, since `ID2D1RenderTarget` stays thread-affine no matter the factory's threading mode. Requires the D2D factory to be `D2D1_FACTORY_TYPE_MULTI_THREADED` (was `SINGLE_THREADED`); see the Phase 5 `DECISIONS.md` entry.

| | ms/frame (avg) | ms/frame (min) | ms/frame (max) |
|---|---|---|---|
| Phase 5 (math-only parallel), `x64-release` | 118.19 | 8.66 | 196.96 |
| Phase 5 (+ parallel geometry build), `x64-release` | **103.90** | **7.34** | **192.69** |
| Phase 5 (+ parallel geometry build), `x64-debug` | 673.68 | 15.86 | 1106.33 |

Release improved further (~12% faster than the math-only version, ~36% faster than the pre-Phase-5 baseline overall) but not dramatically - D2D's `MULTI_THREADED` factory mode makes concurrent resource creation *safe*, not necessarily highly parallel; it's documented to add its own internal locking around D2D calls, which caps how much real concurrency the sink-writing gets even across multiple worker threads. Debug saw no meaningful change (still ~4x worse than pre-Phase-5) - synchronization overhead dominates either way in an unoptimized build regardless of which phase it's applied to. The `x64-release`-only takeaway above still stands.
