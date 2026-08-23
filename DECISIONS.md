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
