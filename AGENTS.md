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
returns are marginal now relative to switching the renderer to Direct2D
(GPU-accelerated, part of the Windows SDK, no new external dependency) if
this project keeps needing more headroom as sprite art grows — that would
eliminate this whole category of CPU-resample-cost tuning rather than
continuing to chase it. Not started; would mean rewriting
`viewer_assets.cpp`'s drawing primitives and `render()`'s structure, a real
rendering-backend change, not a tweak.

## Key Scope Boundaries

- `src/core/` is the deterministic simulation; avoid viewer-only changes there.
- `src/apps/viewer_win32.cpp` owns the window, input, layout, and render loop.
- `src/apps/viewer_assets.cpp` owns GDI+ sprite loading/drawing and the
  procedural fallback.
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
