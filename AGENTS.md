# Pulse Court — Agent Notes

This file captures project-level notes useful for future development work.

## Build & Verification Commands

The project uses CMake with the Visual Studio 2026 generator (named
`Visual Studio 18 2026` by CMake). A typical isolated build and test pass is:

```cmd
cd "C:\Users\kaust\OneDrive\Documents\AI Omega Strikers"
cmake -B build/release -G "Visual Studio 18 2026" -A x64 -DPULSE_BUILD_VIEWER=ON
cmake --build build/release --config Release

# Run tests
ctest --test-dir build\release -C Release --output-on-failure

# Deterministic headless smoke test
build\release\Release\pulse_headless.exe --benchmark 100000 --left kite --right bastion

# Canonical viewer launch (default setup screen)
powershell -ExecutionPolicy Bypass -File .\scripts\run_viewer.ps1

# Observer-only visual QA on a specific monitor
powershell -ExecutionPolicy Bypass -File .\scripts\run_viewer.ps1 -Monitor scepter -Passive

# Desktop shortcut script (quote paths with spaces)
powershell -ExecutionPolicy Bypass -File .\scripts\create_desktop_shortcut.ps1 -OutputPath "C:\Temp\Pulse Court.lnk"

# Sprite pipeline -- run after ANY source sprite sheet changes (clean_sprites
# first only if player body sheets changed), then rebuild so the CMake
# post-build step copies the refreshed assets/sprites/display/ output
python scripts/clean_sprites.py
python scripts/bake_display_sprites.py
```

> **Deprecation note:** old isolated build folders such as `build\visual-*` are
> disposable local QA artifacts and are not a supported launch surface. The
> canonical build path is `build\release` and the canonical launcher is
> `scripts\run_viewer.ps1`.

## Viewer Assets

The Win32/GDI viewer loads PNGs from the `assets/` directory that is copied
next to `pulse_viewer.exe` by a post-build CMake step. `src/apps/viewer_assets.cpp`
contains the runtime `SheetId`/`kSheets` table, which is the actual source of
truth for what gets loaded; `assets/sprites/animated/manifest.json` documents
the same file paths, frame conventions, and (as of v3) each sheet's
`displayTile` bake target, and is meant to stay in sync with `kSheets` by hand.

### Asset pipeline

Three stages, each a separate script, run in order when source art changes:

1. **Author** — raw AI-generated sheets land in `assets/sprites/animated/`
   (body sheets) or directly in `assets/` (arena, one-offs).
2. **`scripts/clean_sprites.py`** — content-aware cleanup for player body
   sheets only (AI-generated frames don't respect hard cell boundaries; this
   isolates each pose via connected-component analysis and repacks into a
   uniform, correctly-anchored tile grid). Output: `assets/sprites/clean/`,
   currently 456px tiles.
3. **`scripts/bake_display_sprites.py`** — pure downscale pass, run over
   every sheet in the manifest (body sheets from `sprites/clean/`, VFX/icons
   straight from `sprites/animated/`) down to that sheet's `displayTile` size
   in manifest.json. Output: `assets/sprites/display/`, which is what
   `AssetManager::kSheets` actually points at. Needed because nothing is ever
   drawn anywhere near the 456-2172px master art size — skipping this step
   meant GDI+ resampled far more source pixels than were ever displayed,
   every single frame. **Read the script's `SIZE_NOTES` docstring before
   changing any `displayTile` value or `kCourtInternalMaxWidth`** — it documents
   a real regression hit while tuning these (see Render performance below):
   the cost that matters is the source/destination size *mismatch*, not
   either size alone, so tile sizes can't just be scaled proportionally with
   the internal resolution cap.

Re-run stage 3 (and stage 2 first, if body sheets changed) whenever source
art changes, then rebuild so the post-build copy picks up the refresh.

### Render performance

`render()` was CPU-bound on GDI+ at ~100-110ms/frame (~9-10 FPS) regardless
of the render FPS cap, and got worse the larger the window. Fixed across two
rounds, all figures from real timing instrumentation (temporarily added,
measured, removed — never asserted without measuring, because more than one
plausible-sounding guess in this section turned out to be wrong when
actually measured — see the callouts below):

1. `AssetManager::draw_image_cropped` (the static arena background) caches
   its high-quality resample in a GDI-compatible bitmap keyed by `(id, w, h)`
   and only regenerates it on a real size change (e.g. window resize).
   Before this it re-ran `InterpolationModeHighQualityBicubic` over the full
   arena image every frame for no visual benefit, costing ~40ms/frame alone.
   **The cache-hit path matters too**, not just the cache-miss/regenerate
   path: an earlier version stored the cache as a `Gdiplus::Bitmap` and
   re-drew it every frame via `Graphics::DrawImage` even at
   `InterpolationModeNearestNeighbor` (same size, "should" be nearly free) —
   that GDI+ call's cost still scaled with image size and cost ~19-24ms/frame
   once the internal buffer got large (see point 2). Switched to storing the
   cache as a plain GDI compatible bitmap (`HBITMAP`/`HDC`, see `~AssetManager`
   for cleanup) and blitting it with raw `BitBlt`, which is negligible even
   at large sizes. Lesson: GDI+'s `Graphics` abstraction has real per-pixel
   cost for a full-image operation independent of interpolation mode; for a
   same-size cached copy, skip GDI+ entirely and use `BitBlt`.
2. Court content (arena + lines + entities) renders into an offscreen buffer
   capped at `kCourtInternalMaxWidth` (2560px — see the pipeline note above
   before changing this), then one blit upscales it to the actual on-screen
   court rect. Debug overlays (`F1`/`F2`) and the footer still draw directly
   at native on-screen resolution, after the buffer blit, so they stay crisp
   regardless of the internal cap.
3. The buffer-to-window blit (point 2) uses plain GDI `StretchBlt` with
   `COLORONCOLOR`, not `HALFTONE`. This blit is always a magnification (the
   buffer is capped smaller than most on-screen court rects), and
   `HALFTONE`'s cost comes from dithered averaging that exists to improve
   *minification* quality — it bought nothing here but cost ~16-19ms at a
   2560px-wide window (`COLORONCOLOR`: ~1-2ms for the same blit). A GDI+
   `Graphics::DrawImage` alternative (for genuine bilinear quality) was
   measured and rejected: wrapping the buffer via
   `Gdiplus::Bitmap(HBITMAP, HPALETTE)` does a full pixel-format conversion
   on every call, not a cheap wrap — it measured **~206ms**, worse than
   `HALFTONE`. If smoother-than-`COLORONCOLOR` upscaling is ever wanted, the
   internal buffer would need to be GDI+-native from the start to avoid that
   conversion cost — see the GPU/Direct2D note below for why that's probably
   not the best place to spend that effort.
4. The asset pipeline's `sprites/display/` bake step means
   `AssetManager::draw_sprite` resamples from small source tiles instead of
   456-2172px masters — but **how small matters in both directions**. A
   first pass scaled every `displayTile` proportionally with the cap
   increase in point 2 (192px → 448px for body sheets) on the assumption
   that per-sprite cost was dominated by fixed per-call overhead, since an
   earlier test (varying only *destination* size, at a fixed 192px source)
   had measured entity cost flat across a 4x destination-pixel-area range.
   That assumption was wrong for *source* size: measured entity render time
   went from ~6.5ms to ~28ms at the same destination size, because a larger
   source relative to destination means more resampling work, which is
   exactly the problem this bake step exists to solve — reintroduced at a
   smaller scale by making the source too big again. Current values (see the
   script's `SIZE_NOTES` for the full reasoning) pick each tier's tile size
   close to its typical on-screen draw size rather than adding headroom for
   its own sake: 48px icons, 128px core/strike-scale VFX, 256px player-scale
   (body sheets, jetstep FX, goal burst — empirically curve-fit since it's
   drawn every frame for both players), 384px area-scale (Anchor Well, Pulse
   Gate — not curve-fit, low duty cycle).

Measured result, default-size (1300x800 window): total frame time
**~100-110ms → ~10-13ms** (~9-10 FPS → ~78-100 FPS).

Measured result, maximized (2560px-wide window): total frame time
**~35-39ms (after round 1 alone) → ~20-28ms** (~26-28 FPS → ~36-50 FPS).
Entity cost stays roughly flat regardless of window size (~8-11ms at this
window width, similar order of magnitude to default-size). Not
independently re-verified: an actual user maximizing via mouse click/title
bar, on real hardware (all figures above are from this session's synthetic
`SC_MAXIMIZE` `PostMessage` test, run on the dev machine).

**Remaining, lower-priority cost:** `AssetManager::draw_sprite` still pays
fixed per-call overhead from constructing a fresh `Gdiplus::Graphics` +
`ImageAttributes` per call at `InterpolationModeHighQualityBicubic`/
`SmoothingModeAntiAlias`. Reusing one `Graphics` per `render()` pass and
skipping `ImageAttributes` when alpha is opaque would shave more, but
returns are marginal relative to the Direct2D migration below, which
eliminates this whole category of CPU-resample-cost tuning rather than
continuing to chase it.

### Direct2D migration (in progress)

Full plan: `docs/d2d-migration-plan.md`. Branch `d2d-migration` off the
`isometric-experiment` checkpoint that has all of the GDI perf work above.
Brick-by-brick: a flag-gated spike first, then per-screen-mode cutovers
(gallery → setup → gameplay), then legacy GDI/GDI+ removal — GDI stays fully
functional until the final removal task, so any brick can be reverted
cleanly by returning to `isometric-experiment`.

**Tasks 1-4 implemented; gameplay migration remains open.** The checkpoint,
spike, gallery cutover (`ed3fd57`), and setup cutover (`293d3a4`) are committed.
Gallery/setup use D2D by default; gameplay retains the GDI capped-buffer path.
The historical spike measurements below do not establish gameplay performance.
Task 2 added
`src/apps/viewer_d2d.{hpp,cpp}` (`D2DContext`: `ID2D1Factory`,
`IDWriteFactory`, `IWICImagingFactory`, `ID2D1HwndRenderTarget`, one shared
brush, all `Microsoft::WRL::ComPtr`) and a `--d2d-spike` flag that
short-circuits `render()` into drawing one sprite (`assets/arena.png`) via
Direct2D; it originally left the GDI path untouched when absent. Two lifecycle
traps found by manual review and fixed (both are exactly the "cheap-looking
wrap that isn't" class of bug this project has hit before with GDI+):

- **COM shutdown ordering.** The three COM factories must be released
  *before* `CoUninitialize()`, not just the device-dependent render
  target/brush. `g_d2d_ctx` has static storage duration, so if only
  `discard_device_resources()` (render target + brush) runs at shutdown, the
  factories are released later at program-exit static teardown — *after*
  `CoUninitialize()` already tore down the apartment, which is undefined
  behavior. Fixed with a separate `release_all_resources()` (all six
  `ComPtr` members) called immediately before `CoUninitialize()`;
  `discard_device_resources()` stays narrowly scoped since it also runs on
  every `D2DERR_RECREATE_TARGET` device-loss recreate, where the factories
  must *not* be released.
- **Device-loss bitmap invalidation.** `ID2D1Bitmap` is device-dependent on
  the specific render target that created it. Caching the loaded sprite in a
  function-local `static` (rather than a `D2DContext` member reset alongside
  `render_target`/`brush`) meant a device-loss recreate would skip reloading
  and draw a bitmap belonging to the just-destroyed target. Fixed by moving
  the bitmap onto `D2DContext` and resetting it inside
  `discard_device_resources()`.

**Measured** (`QueryPerformanceCounter`, 50x `DrawBitmap` per frame,
instrumentation added then stripped, never committed): steady-state
**~0.4-0.9ms** at both 1300x800 and maximized 2560x1369 — draw cost does not
scale with window size, confirming the GPU-headroom thesis the rest of the
migration is betting on, versus GDI's ~10-13ms/~20-28ms above.

**Not independently confirmed — deferred to the user, per the plan's task-2
evaluator gate:** visual correctness (alpha-edge fringing, resize
stretch-smear, no-flag pixel parity vs GDI) and the Ctrl+Win+Shift+B
device-loss recovery check. Screenshot automation is unreliable/unsafe for
this app's `GetDC`+`BitBlt` rendering style (bypasses `WM_PAINT`/DWM
redirection — see memory), and the device-loss hotkey restarts the graphics
driver system-wide. Crash-safety (resize stress, launch/exit with and
without the flag) was verified via process-liveness smoke tests.
Tasks 3-4 were implemented later, but confirmation of these visual/device-loss
checks was not found in the September readiness audit. Reconcile that evidence
before the task-5 gameplay cutover; do not silently mark the gates passed.

## Key Scope Boundaries

- `src/core/` is the deterministic simulation; avoid viewer-only changes there.
- `src/apps/viewer_win32.cpp` owns the window, input, layout, and render loop.
- `src/apps/viewer_assets.cpp` owns GDI+/D2D sprite loading/drawing and the
  procedural fallback.
- `src/apps/viewer_d2d.cpp` owns Direct2D/DirectWrite/WIC resources.
- `src/apps/viewer_monitor.cpp` owns monitor enumeration and placement.

## Notes

- The viewer uses a 120 Hz fixed simulation accumulator (`kSimDt`) and an
  independently-throttled render loop (`render_throttled`, default 60 FPS,
  cycled with `-`/`=` through 30/60/90/120/144/Uncapped). The two are fully
  decoupled: `Simulation::step` advances on wall-clock delta accumulated
  against `kSimDt` regardless of how often `render()` runs, so changing the
  render cap changes presentation smoothness only, never sim or animation
  speed.
- All animation/VFX timing (`AnimationController` body/persistent/vfx
  `elapsed` counters) is counted in simulation ticks: it only advances inside
  `AnimationController::update()`, which is only called once per
  `Simulation::step()` (see `advance_tick` in `viewer_win32.cpp`). Any future
  gameplay effect that locks player input for a duration (e.g. a dash/ability
  freeze) belongs in `src/core/` as a tick counter for the same reason —
  render-frame-driven timing must never gate sim/animation state.
- Body animation frame selection (`resolve_body_frame` in
  `viewer_assets.cpp`) scales by each sprite sheet's actual column count
  rather than a hardcoded frame count, and per-animation durations live in one
  set of named tick constants (`kIdleLoopTicks`, `kMoveLoopTicks`,
  `kMoveTransitionTicks`, `kStrikeAnimTicks`, `kDashAnimTicks`,
  `kAbilityAnimTicks`, `kGoalAnimTicks`) shared by the state machine, the
  frame math, and the procedural-fallback pose transforms. Adding more frames
  to a sheet later only requires updating `AssetManager::kSheets`' column
  count; no per-animation formula changes needed.
- Action inputs (Strike/Ability/Dash) are edge-triggered; movement/aim are
  continuous from the keyboard state.
- Window focus loss clears the keyboard state, pending input, and simulation
  input to prevent stuck keys.
- QA/debug controls: `Space` pause, `N` step one sim tick while paused, `[` and
  `]` cycle sim speed (0.25x / 0.5x / 1.0x), `-`/`=` cycle the render FPS cap,
  `F1` hitbox overlay, `F2` animation debug overlay, `F3` pseudo-3D compression
  toggle (parked experiment — shrinks the court more than it helps; the
  isometric look is being pursued via sprite art instead, not further
  viewer-side transforms), `B` toggle deterministic P2 bot.
- `--gallery` launches a sprite-sheet gallery for inspecting every loaded sheet
  and frame.

## Challenge / Pulse Zero deployment stack (added 2026-07-18)

The `src/ai/` library implements stage 4 of the Pulse Zero phase-3 plan
(see `build/pulse-zero-work/docs/pulse-zero-phase3-certification-deployment-plan.md`;
that directory is a separate read-only clone holding the training pipeline —
its `training/runs/pulse-zero-phase2/` is frozen scientific evidence, never
write there).

- `pulse_policy.{hpp,cpp}` — real PZPB bundle loader (fail-closed strict
  `Policy::load`, non-certified `load_preview`) + FP32 MLP inference.
  Strict loading is blocked while the compiled core is ruleset 2 and the
  supported policy contract is ruleset 4 (core merge is a follow-up).
  The September runtime fix adds the compiled-ruleset guard; the original
  July implementation checked only literal manifest ruleset 4 and did not
  enforce this boundary despite the previous wording here.
  `pulse_core_compat.hpp` documents the degraded touch-kind shim.
- `pulse_policy_devgen` + `pulse_dev_bundle.exe` — deterministic
  NON-CERTIFIED dev bundles for structural testing only.
- Two session classes coexist deliberately: `pulse::ChallengeSession`
  (`challenge_session.hpp`, simple evidence logger used by headless smoke)
  and `pulse::challenge::ChallengeSession` (`challenge_protocol.hpp`,
  integrity/provenance protocol session used by the viewer). Consolidation
  is a known follow-up; don't half-merge them.
- Headless: `pulse_headless --challenge-smoke` with explicit `--scripted-policy`, or
  `--policy-bundle-preview <pzpb>` for real preview inference; records
  per-game `.pulse` replays. Viewer: `--replay <file.pulse>` historical
  visualizer; `--challenge` runs the 20-game protocol (strict or preview
  bundle), append-only logs, quit-after-completion is legitimate.
  Strict smoke must retain and execute its loaded policy; missing strict
  bundle paths are errors, never an implicit request for scripted mode.
  Smoke evidence records controller mode and network decisions. Six-game
  smoke is separate from the human challenge and never establishes AI strength.

Current checkpoints and remaining scope: `ai-docs/project-status.md`.

### Session conventions (hard-won)

- Never run two MSBuild builds concurrently in the same build dir (locked
  .tlog hangs). Orchestrating agents build centrally and serially.
- All test mains call `pulse::test::disable_error_dialogs()`
  (`tests/test_no_dialogs.hpp`) so Debug assert/abort failures write to
  stderr and exit instead of hanging on a modal CRT dialog.
- Run the Debug test suite too, not just Release: `assert(...)` around
  side-effecting calls is a no-op under NDEBUG and has hidden real bugs
  here twice (evidence validation, CLI argc off-by-ones).
