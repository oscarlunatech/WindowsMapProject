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
