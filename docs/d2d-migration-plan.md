# Direct2D Migration — Handoff Plan

Produced by the orchestrator Planner phase (2026-07-13) to replace `pulse_viewer`'s
GDI/GDI+ rendering backend with Direct2D + DirectWrite + WIC. Not yet started —
see "Status" at the bottom before picking this up.

## Goal

Migrate `pulse_viewer`'s rendering backend from GDI/GDI+ (CPU-bound, ~36-50 FPS
maximized) to Direct2D + DirectWrite + WIC (GPU-accelerated), delivering: (a) frame
cost that no longer scales with window size or sprite source size, (b) proper
antialiasing on shapes and sprites, (c) removal of the internal-resolution-cap
workaround, and (d) a measured verdict on whether the offline sprite-baking
pipeline is still needed. The deterministic simulation (`src/core/`) and
`pulse_headless` are untouched. Delivery is brick-by-brick: a flag-gated spike
first, then per-screen-mode cutovers (gallery → setup → gameplay), then legacy
removal — the GDI path stays functional until the final removal brick, so any
brick can be reverted cleanly.

## Constraints

- **No changes under `src/core/`.** Verify with `git diff --stat -- src/core`
  being empty at every commit. `pulse_headless` must not gain any viewer/GDI/D2D
  dependency (it links only `pulse_core` in CMakeLists.txt — keep it that way).
- All work happens in `src/apps/viewer_*`, `CMakeLists.txt` (viewer target only),
  `AGENTS.md`, `docs/architecture.md`, and (task 7 only, measurement-side)
  possibly `assets/sprites/animated/manifest.json` + `scripts/bake_display_sprites.py`
  docs.
- **Adding `d2d1`, `dwrite`, `windowscodecs`, `ole32` to the viewer's link line is
  intentional and approved** — it is part of this task, not an architecture
  violation. Update `docs/architecture.md`'s "Boundaries" section (currently:
  "links only `pulse_core`, `user32`, `gdi32`") in the same commit that changes
  the link line.
- **Measurement discipline (non-negotiable, established project style):** never
  claim a perf or visual win without a measurement or a described manual check
  the user can run. Pattern: temporarily add `QueryPerformanceCounter`-based
  timing around `render()` (or the relevant sub-block), log ms per frame, record
  numbers at BOTH default window size (1300x800) and maximized (~2560px wide),
  then strip the instrumentation before committing. Baselines to beat (from
  AGENTS.md "Render performance"): default ~10-13ms/frame (~78-100 FPS),
  maximized ~20-28ms (~36-50 FPS). The app's built-in FPS counter (bottom-right)
  and `-`/`=` FPS-cap hotkeys (set to Uncapped for measurement) are available in
  every build.
- Update `AGENTS.md` incrementally as durable decisions land in each task (don't
  defer all doc updates to the end).
- Build/run commands: `cmake --build build/release --config Release`; run
  `build\release\Release\pulse_viewer.exe` (setup screen), `--gallery` (gallery
  mode), `--left kite --right vale` (straight to gameplay), or
  `scripts\run_viewer.ps1`. Tests: `ctest --test-dir build\release -C Release
  --output-on-failure`.
- Sim/animation timing must be unaffected: all animation advances in sim ticks
  via `AnimationController::update()` (once per `Simulation::step()`), never per
  render frame. Nothing in this migration may change `advance_tick`, the
  `kSimDt` accumulator, or `render_throttled`'s throttle logic.
- Each task = one commit on the migration branch (see Task 1). One giant diff is
  not acceptable.

**Architecture decision (made here, executors should not relitigate):** use
`ID2D1HwndRenderTarget`, NOT the D2D1.1 `ID2D1DeviceContext` + DXGI swap chain
path. Rationale: single-window dev visualizer, no D3D interop, no
DirectComposition, no dirty-rect needs; HwndRenderTarget halves the scaffolding,
handles present internally, and has a well-defined device-lost contract
(`D2DERR_RECREATE_TARGET` from `EndDraw`). Cost accepted: `DrawBitmap` on this
target supports only NEAREST_NEIGHBOR and LINEAR interpolation (no
HIGH_QUALITY_CUBIC) — acceptable because baked display sprites are already near
their on-screen draw size (and this feeds Task 7's pipeline verdict). All D2D
state is centralized in one `D2DContext` struct precisely so a later swap-chain
upgrade would be localized. Create the render target with
`D2D1_PRESENT_OPTIONS_IMMEDIATELY` so `EndDraw` does not block on vblank — this
preserves the existing render-FPS-cap feature's semantics (including
"Uncapped") and keeps frame-time measurements comparable to the GDI baselines;
`render_throttled` remains the sole pacing mechanism.

## Tasks

**1. Git pre-flight — checkpoint commit + migration branch (rollback anchor).**
The working tree on branch `isometric-experiment` is dirty and nothing from this
session is committed: modified `AGENTS.md`, `assets/arena.png`,
`assets/sprites/animated/manifest.json`, `src/apps/viewer_assets.{cpp,hpp}`,
`src/apps/viewer_win32.cpp`; untracked `scripts/bake_display_sprites.py`,
`assets/sprites/display/` (baked output), and three stray PNGs (`assets/1.png`,
`assets/2.png`, `assets/1702f1fc-0462-41e4-a499-6d10ec07e4e4.png`). Commit the
session's work as a checkpoint on `isometric-experiment` (include the bake
script and `assets/sprites/display/`; ask the user — or flag in the commit
message — whether the three stray PNGs are wanted; do not silently delete
them). Then create branch `d2d-migration` from that commit. **This commit is the
rollback point:** abandoning the port at any later task = park/delete
`d2d-migration`; `isometric-experiment` retains the fully-working GDI viewer
with all of this session's perf fixes. Verify: `git status` clean, `git log`
shows checkpoint, viewer still builds and runs from the checkpoint.

**2. New files `src/apps/viewer_d2d.hpp/.cpp` + `CMakeLists.txt` +
`docs/architecture.md` — D2D spike: scaffold + exactly one sprite on screen,
behind a `--d2d-spike` flag. [EVALUATOR GATE — do not dispatch tasks 3+ until
this passes.]**
Create a `D2DContext` struct owning: `ID2D1Factory` (SINGLE_THREADED),
`IDWriteFactory`, `IWICImagingFactory` (requires `CoInitializeEx(nullptr,
COINIT_APARTMENTTHREADED)` at startup, `CoUninitialize` at shutdown, after all
COM objects are released — shutdown ordering is a known footgun; use
`Microsoft::WRL::ComPtr` from `<wrl/client.h>`), and device-dependent resources
behind two methods: `create_device_resources(HWND)` (creates
`ID2D1HwndRenderTarget` with `D2D1_PRESENT_OPTIONS_IMMEDIATELY`, pixel format
`DXGI_FORMAT_B8G8R8A8_UNORM`/`D2D1_ALPHA_MODE_IGNORE`, then `SetDpi(96, 96)` so
1 DIP == 1 client-rect pixel, matching all existing integer pixel math; one
shared `ID2D1SolidColorBrush`; antialias mode
`D2D1_ANTIALIAS_MODE_PER_PRIMITIVE`) and `discard_device_resources()`.
Device-lost contract wired from day one: frame = if no RT →
`create_device_resources`; `BeginDraw` … `EndDraw`; if `EndDraw` returns
`D2DERR_RECREATE_TARGET` → `discard_device_resources()` and return (next frame
recreates). Handle `WM_SIZE` in `WndProc` (viewer_win32.cpp) by calling
`rt->Resize(D2D1::SizeU(w,h))` — without this, D2D stretches stale content on
resize. Add a `COLORREF`→`D2D1::ColorF` helper (watch byte order: COLORREF is
0x00BBGGRR; `GetRValue/GetGValue/GetBValue`, divide by 255). WIC load path for
ONE image (suggest `assets/arena.png`, the largest at ~2200x1300):
`CreateDecoderFromFilename` → `GetFrame(0)` →
`WICConvertBitmapSource(GUID_WICPixelFormat32bppPBGRA, …)` →
`rt->CreateBitmapFromWicBitmap` — **once at load, never per frame** (the
per-frame variant is this API family's version of the
`Gdiplus::Bitmap(HBITMAP)` trap). When `--d2d-spike` is on the command line,
`render()` short-circuits into the D2D path: Clear(black), `DrawBitmap` the
sprite at a fixed on-screen rect with `D2D1_BITMAP_INTERPOLATION_MODE_LINEAR`,
present. GDI path completely untouched when flag absent. CMake: add
`viewer_d2d.cpp` to `pulse_viewer` sources and `d2d1 dwrite windowscodecs ole32`
to its `target_link_libraries`; same commit updates `docs/architecture.md`
Boundaries to list the new libs as intentional. Verify: (a) builds clean at
/W4; (b) `pulse_viewer.exe --d2d-spike` shows the arena image, alpha edges look
correct (no black fringing — fringing means premultiplied-alpha conversion was
skipped); (c) window resize doesn't crash or stretch-smear; (d) close exits
cleanly (exit code 0, no COM/D2D leak asserts in debug output); (e) without the
flag, viewer is pixel-identical to before (launch and eyeball setup +
gameplay); (f) temporarily instrument frame time in spike mode with the sprite
drawn 50x per frame in a loop, record ms at default + maximized window, strip
the loop and instrumentation — this is the "GPU headroom is real" measurement;
expect ~1-3ms CPU-side; (g) `ctest` passes, `git diff --stat -- src/core`
empty; (h) manual device-loss check described to the user: press
Ctrl+Win+Shift+B (Windows graphics-driver restart hotkey) while the spike
runs — app must recover within a frame or two, not crash or go black
permanently.

**3. `src/apps/viewer_assets.{hpp,cpp}` + `viewer_win32.cpp` (gallery only) —
AssetManager grows D2D bitmap storage + D2D sprite/text/line drawing; gallery
mode cuts over. Serial after task 2.**
AssetManager keeps its `Gdiplus::Bitmap` array (gameplay/setup still use it
until task 5 — do NOT delete it early) and additionally loads every sheet in
`kSheets` into an `ID2D1Bitmap` array via the task-2 WIC path. Because
`ID2D1Bitmap` is device-dependent on an HWND render target, structure this as
`AssetManager::create_device_bitmaps(D2DContext&)` / `release_device_bitmaps()`,
invoked from the D2DContext device-loss lifecycle (recreate by re-decoding from
disk — device loss is rare, a sub-second reload hitch is acceptable; do not
keep permanent system-memory copies). Store each sheet's pixel size at load so
callers stop reaching into `Gdiplus::Bitmap*` (render_gallery currently calls
`g_assets->get(sheet)->GetWidth()` — add a `sheet_size(SheetId)` accessor). New
drawing entry points (D2D twins; legacy HDC versions stay untouched):
- `draw_sprite` (D2D overload taking `D2DContext&`): same frame/grid math as
  the GDI+ version (lines 357-418 of viewer_assets.cpp), then
  `DrawBitmap(destRect, opacity=alpha, LINEAR, srcRect)`. Alpha is a plain float
  parameter — the whole `ImageAttributes`/`ColorMatrix` dance disappears.
  Rotation: `SetTransform(D2D1::Matrix3x2F::Rotation(angle_deg, center))`
  before, **and restore the identity transform after** — unlike the per-call
  `Gdiplus::Graphics`, the render target is persistent, so a leaked transform
  corrupts every subsequent draw (top new-API trap in this port).
- `draw_text` D2D version: `IDWriteTextFormat` ("Consolas",
  `DWRITE_FONT_WEIGHT_NORMAL`), cached per pixel-height in a small map (only ~4
  sizes exist: 12/14/16/18); convert the incoming `char*` (UTF-8/ASCII) via
  `MultiByteToWideChar`; `DrawTextW` into a wide layout rect at (x, y). GDI
  `lfHeight` (cell height) ≠ DWrite em size: start with `fontSize = height *
  0.85f` and tune the single constant against a GDI screenshot until sidebar
  text visually matches. Replace the stateful `SetTextAlign(TA_CENTER)` pattern
  (used in `render_gallery` and `render_anim_debug`) with an explicit alignment
  parameter (default left) mapped to `DWRITE_TEXT_ALIGNMENT_CENTER` on a rect
  centered on x.
- `draw_line` D2D version: shared brush + `DrawLine` with stroke width.
Cut over gallery mode: in `render()`, when `g_gallery_mode`, run the frame
through D2DContext (Clear → `render_gallery` using the new helpers → EndDraw);
setup/gameplay frames still take the legacy GDI path (a frame must be all-D2D
or all-GDI — never mix within one presented frame). Verify: (a)
`pulse_viewer.exe --gallery` — page through EVERY sheet (Up/Down) and frames
(Left/Right); this exercises all 35 sheets' decode + draw; (b) check
specifically for frame-edge bleed: with LINEAR sampling, `DrawBitmap` can
sample texels adjacent to the source rect, showing slivers of neighboring
frames at tile edges — if visible, inset source rects by half a texel (known
remedy; body sheets have an 8px safety border so VFX sheets are the likely
offenders); (c) colors match the GDI+ rendering (WIC color-profile handling can
shift colors — compare a gallery screenshot per backend); (d) header/footer
text legible, centered as before; (e) startup time: instrument `AssetManager`
D2D load once, record ms (this is the load-time baseline task 7 compares
against), strip instrumentation; (f) setup + gameplay modes still render via
GDI, unchanged; (g) ctest passes, core untouched.

**4. `src/apps/viewer_assets.cpp` + `viewer_win32.cpp` (setup only) —
rect/circle D2D helpers; setup screen cuts over. Serial after task 3.**
Add D2D twins for `draw_rect` (FillRectangle + DrawRectangle, 1px stroke) and
`draw_circle` (FillEllipse + DrawEllipse, 2px stroke) using the shared brush.
Cut `render_setup` over to the D2D frame path (it uses only draw_rect,
draw_text, draw_circle, draw_sprite — all now available). Mouse hit-testing in
`handle_setup_input` uses pure rect math on client coordinates — unaffected,
but verify clicks still land (SetDpi(96,96) in task 2 is what guarantees
coordinate parity). Verify: (a) launch with no args — selection cards,
portraits, ability icons, highlight borders, Start Match button all render;
(b) card borders/selection highlight colors correct (COLORREF byte-order check
applies); (c) mouse selection + Enter start a match (which then renders via the
still-GDI gameplay path); (d) AA visibly improves circle portraits' fallback
rendering; (e) screenshot compare vs GDI setup screen — layout identical within
a pixel or two; (f) note in AGENTS.md if any visual differences are accepted
(e.g. softer 1px borders under AA).

**5. `src/apps/viewer_win32.cpp` + `src/apps/viewer_assets.{hpp,cpp}` —
gameplay frame cutover: restructure `render()`, remove the
internal-resolution-cap buffer, port all remaining helpers and the
AnimationController/court drawing chain. Serial after task 4; this is the
perf-critical brick.**
Port the remaining primitives to D2D: `draw_hollow_circle` and `draw_ellipse`
(DrawEllipse; PS_DOT → an `ID2D1StrokeStyle` with `D2D1_DASH_STYLE_DOT`,
created once on the factory — device-independent), `draw_filled_ellipse`
(FillEllipse with brush alpha — replaces the per-call `Gdiplus::Graphics`
construction), `draw_filled_pie` (D2D has no pie primitive:
`ID2D1PathGeometry` — BeginFigure(center) → AddLine(arc start) →
AddArc(`D2D1_ARC_SEGMENT` with end point, radii (r,r), CLOCKWISE,
`D2D1_ARC_SIZE_LARGE` iff sweep > 180°) → EndFigure(CLOSED) → FillGeometry;
per-call geometry creation is fine — it's device-independent and only the F1
overlay uses it), `draw_cooldown_bar` and `draw_court_lines` (compose
already-ported helpers — mechanical). Change
`AnimationController::draw_entities/draw_body/draw_persistent/draw_persistent_fallback/draw_vfx`
and `AssetManager::draw_image_cropped` signatures from `HDC` to `D2DContext&`.
`draw_image_cropped` becomes: same aspect-preserving crop math (lines
434-452), then one `DrawBitmap` with that source rect — **delete the entire
HBITMAP/HDC cache** (fields `cropped_cache_*`, the cache logic, and the
`~AssetManager` cleanup); GPU sampling per frame replaces it. Restructure
`render()`: replace
GetDC/CreateCompatibleDC/CreateCompatibleBitmap/FillRect/BitBlt/ReleaseDC with
BeginDraw/Clear(black)/…/EndDraw through D2DContext, and **delete the
`kCourtInternalMaxWidth` buffer + StretchBlt entirely** —
`draw_court_lines`/`draw_entities` are already parametrized on (court_x,
court_y, court_w, court_h), so call them directly with (draw_x, draw_y, draw_w,
draw_h) at native resolution instead of (0, 0, buf_w, buf_h) into a buffer.
`render_sidebar`, `render_footer`, `render_hitbox_overlay`,
`render_anim_debug`, and the FPS text swap to the D2D helpers (mechanical —
helpers exist). Keep `render_throttled`, the FPS counter deque, and all
input/sim logic byte-identical. Verify — this is where the migration's core
claims get proven, all measured with the temporary-instrumentation pattern,
numbers recorded then instrumentation stripped: (a) gameplay renders
correctly: arena, court lines, both players animating, core, shadows, VFX
(strike/dash/goal/kickoff), persistent effects (test all three characters for
Jetstep/Anchor Well/Pulse Gate), sidebar cards/cooldown bars/trace, footer, F1
pie overlay (compare sweep/orientation against a GDI screenshot —
angle-convention bugs show here), F2 text overlay; (b) frame time at default
1300x800 AND maximized ~2560w with cap set to Uncapped: target maximized
≤10ms and within ~2x of the default-size number (i.e., cost no longer scales
with window area) — record exact before/after table against the AGENTS.md
baselines (default ~10-13ms, maximized ~20-28ms); (c) antialiasing visibly
present on court lines/circles/pie (screenshot for the user — this is the
explicitly requested feature); (d) court content at maximized is now sharper
than GDI (no capped-buffer upscale blur) — note it; (e) 5-minute play session:
no leaks (Task Manager memory flat), no transform leakage artifacts (sprites
drawing rotated when they shouldn't = missed transform restore); (f)
Ctrl+Win+Shift+B during gameplay: recovers, sprites intact after device-loss
recreate (this now exercises `AssetManager::create_device_bitmaps` re-decode);
(g) sim determinism spot check: `pulse_headless.exe --benchmark 100000 --left
kite --right bastion` output unchanged vs task-1 checkpoint, ctest green,
`git diff --stat -- src/core` empty; (h) update AGENTS.md Render performance
section with the new measured numbers.

**6. `src/apps/viewer_assets.{hpp,cpp}`, `viewer_win32.cpp`, `CMakeLists.txt`,
`AGENTS.md`, `docs/architecture.md` — remove the legacy GDI/GDI+ backend.
Serial after task 5.**
Delete: all legacy HDC-signature helper implementations and declarations; the
`Gdiplus::Bitmap` array in AssetManager and its load path (D2D/WIC array
becomes the only storage; `get(SheetId)` returning `Gdiplus::Bitmap*` goes
away — callers were migrated in tasks 3-5); `GdiplusStartup`/`GdiplusShutdown`
and `#include <gdiplus.h>` (viewer_assets.hpp line 11); the `--d2d-spike` flag
and spike-only render path (superseded); `gdiplus` from
`target_link_libraries`. Keep `gdi32`/`user32` only if something still needs
them (the window-class `GetStockObject(BLACK_BRUSH)` background is gdi32;
either keep the link or set `hbrBackground = nullptr` — with main-loop D2D
presentation the class brush is vestigial; prefer nullptr + drop gdi32 if
nothing else uses it, but a retained gdi32 link for a trivial usage is
acceptable — say which in the commit message). Rewrite AGENTS.md's "Viewer
Assets → Render performance" subsection: the GDI+ history collapses to a short
"why we moved to D2D" note preserving the two documented traps (expensive
`Gdiplus::Bitmap(HBITMAP)` wrap; per-pixel `Graphics::DrawImage` cost) plus the
new D2D equivalents learned in tasks 2-5 (per-frame
`CreateBitmapFromWicBitmap`, leaked `SetTransform`); update "Key Scope
Boundaries" (viewer_assets.cpp now owns D2D sprite loading/drawing; add
viewer_d2d.cpp line). Final architecture.md Boundaries pass. Verify: (a) full
rebuild from clean succeeds at /W4 with zero references to
`Gdiplus`/`gdiplus.h` (`grep -ri gdiplus src/apps` empty); (b) all three modes
+ F1/F2 overlays + a full match render correctly (regression pass per task-5
checklist, abbreviated); (c) binary no longer imports gdiplus.dll (`dumpbin
/dependents` or equivalent); (d) ctest green; (e) frame-time spot check
unchanged from task 5's numbers.

**7. Measurement-only verdict: does the sprite-bake pipeline
(`scripts/bake_display_sprites.py`, manifest.json `displayTile`) still earn its
keep? Runs after task 5 (task 6 not required); may run in parallel with task 6
ONLY if AGENTS.md edits are coordinated — otherwise serialize after 6. NO
removal without explicit user/evaluator sign-off.**
Working hypothesis to test, not assume: GPU sampling makes **per-frame** draw
cost independent of source sprite size, but three costs may still justify the
bake: load-time decode, memory/VRAM, and LINEAR-interpolation minification
quality (HwndRenderTarget has no mipmaps or cubic filtering; downscaling 456px
masters ~2x to on-screen size may shimmer/alias vs near-native baked tiles).
Method: temporarily point a representative subset of `kSheets` entries (one
body sheet, one large VFX sheet) at their `sprites/clean/`/`sprites/animated/`
masters instead of `sprites/display/`, rebuild, then measure with temporary
instrumentation: (a) per-frame gameplay cost with master-size sources vs baked
(expect: no meaningful difference — that's the hypothesis); (b)
`AssetManager` load time, baked vs masters (task 3 recorded the baked
baseline); (c) process memory delta (35 sheets at master size is plausibly
hundreds of MB of decoded BGRA vs tens for baked); (d) visual quality
side-by-side in gallery + gameplay (minification shimmer during move-loop
animation is the thing to look for). Restore `kSheets`, strip instrumentation.
Deliverable: a short written verdict in AGENTS.md's asset-pipeline section
with the four measurements, recommending one of: keep pipeline as-is
(rationale becomes load-time/memory/minification-quality, no longer per-frame
CPU); keep but relax `displayTile` values upward for quality headroom
(per-frame penalty is gone, so the tight curve-fitting described in the
script's SIZE_NOTES is obsolete — document that the old CPU-resample rationale
no longer applies); or remove — **removal itself is out of scope for this task
and requires user sign-off**. Also record in AGENTS.md that
`kCourtInternalMaxWidth` and the capped-buffer architecture were removed in
task 5 and why.

## Risks

- **Frame-edge bleed:** `DrawBitmap` with LINEAR interpolation can sample
  outside the source rect, showing neighbor-frame slivers on sprite-sheet
  tiles. Remedy: half-texel source-rect inset. Checked in task 3's gallery pass
  (VFX sheets most at risk; body sheets have an 8px baked safety border).
- **Transform leakage:** the persistent render target (vs per-call
  `Gdiplus::Graphics`) means a rotated `draw_sprite` that fails to restore the
  identity transform corrupts all subsequent draws. Explicit restore required;
  checked in task 5(e).
- **Text metrics mismatch:** GDI cell-height vs DWrite em-size — UI text could
  uniformly shrink/grow and break the hand-tuned sidebar layout. Mitigated by a
  single tunable constant in the new `draw_text` plus side-by-side screenshots
  (task 3).
- **Device-loss path is hard to exercise in CI:** mitigated by the concrete
  manual check (Ctrl+Win+Shift+B graphics-driver restart) in tasks 2 and 5; the
  recovery structure (create/discard device resources, `D2DERR_RECREATE_TARGET`
  handling) is built in the spike so it's load-bearing from day one, not bolted
  on.
- **Analogous "cheap-looking wrap that isn't" traps (per the GDI+ lessons):**
  per-frame `CreateBitmapFromWicBitmap` (decode + GPU upload per call),
  `ID2D1GdiInteropRenderTarget::GetDC` (full copy + flush — deliberately NOT
  used in this plan; frames are all-D2D or all-GDI per mode instead), and
  per-draw `Flush()` (defeats batching — the current `gfx.Flush()` calls have
  no D2D equivalent and must not be ported).
- **Temporary double memory (tasks 3-5):** AssetManager holds both GDI+ and D2D
  bitmaps until task 6. Bounded and intentional — an executor must not "clean
  up" the GDI+ array early or gameplay/setup break mid-sequence.
- **Present semantics:** `D2D1_PRESENT_OPTIONS_IMMEDIATELY` can tear (like
  today's BitBlt path); choosing vsync instead would silently break the
  Uncapped FPS mode and make measurements incomparable. If the user later wants
  vsync, it's a one-line change in `D2DContext` — noted, not done here.
- **AA softness on 1px UI borders:** antialiased rects at integer coordinates
  render half-pixel-soft edges. Accepted (AA was requested); if a specific
  border looks bad, the fix is 0.5px offsets on that call site.
- **Color shifts from WIC decoding** (color-profile handling differs from
  GDI+) — screenshot-compared in task 3.
- **Uncommitted working tree:** the entire session's perf work exists only in
  the working tree; task 1 exists precisely to make the rollback point real
  before any migration code lands. Three stray PNGs in `assets/` need a user
  call.
- **Rollback:** at any point, `git checkout isometric-experiment`
  (post-checkpoint) restores the working GDI viewer. Tasks 2-4 additionally
  keep GDI as the default/majority path, so even on the migration branch a bad
  brick degrades only the mode it touched.

## Verification criteria

For the Evaluator, per task (task-local criteria are embedded above; these are
the cross-cutting gates):
1. **Scope guard, every task:** `git diff --stat -- src/core` empty;
   CMakeLists.txt changes confined to the `pulse_viewer` target;
   `pulse_headless` links only `pulse_core`.
2. **Determinism guard, tasks 5-6:** `ctest` green; `pulse_headless.exe
   --benchmark 100000 --left kite --right bastion` output identical to the
   task-1 checkpoint run.
3. **No unmeasured claims:** any task asserting a perf outcome must include
   recorded numbers (ms/frame at default 1300x800 AND maximized ~2560w,
   instrumentation added-then-stripped) or, for visual claims, a
   screenshot/manual check the user can reproduce. Executor reports without
   numbers are incomplete.
4. **Gate after task 2 (spike):** proceed only if the spike shows the sprite
   rendered correctly (alpha, no fringing), clean startup/shutdown/resize,
   device-loss recovery via Ctrl+Win+Shift+B, and ~1-3ms-class frame cost in
   the 50-sprite stress check. If the spike fails on target hardware, invoke
   the rollback (park `d2d-migration`) and stop.
5. **End-state acceptance (after task 6):** all three modes plus F1/F2
   overlays render correctly; maximized-window frame time ≤10ms and no longer
   scaling with window area (vs GDI baseline ~20-28ms); antialiasing visible on
   shapes (screenshot); no `Gdiplus` references or gdiplus.dll import;
   AGENTS.md and docs/architecture.md reflect the new backend and preserved
   trap knowledge; every task landed as its own commit on `d2d-migration`.
6. **Task 7 acceptance:** four measurements recorded (per-frame master-vs-baked,
   load time, memory, minification quality) and a written keep/relax/remove
   recommendation in AGENTS.md — with no pipeline files deleted.

### Critical Files for Implementation
- `src/apps/viewer_assets.cpp`
- `src/apps/viewer_assets.hpp`
- `src/apps/viewer_win32.cpp`
- `CMakeLists.txt`
- `AGENTS.md`

## Status

**Planned, not started.** This file is the Planner-phase output of the
orchestrator skill, produced 2026-07-13 in the same session that did the GDI/GDI+
CPU-side performance work documented in AGENTS.md's "Render performance"
section. Execution (Executor phase — dispatching agents against these tasks)
has not begun. Two things need explicit user sign-off before Task 1 can run:
(1) confirmation to create the checkpoint commit described in Task 1 (nothing
this session has been committed yet), and (2) a decision on the three stray
PNGs in `assets/` (`1.png`, `2.png`, `1702f1fc-0462-41e4-a499-6d10ec07e4e4.png`)
— keep, or something else.

To resume: read this file, confirm the two sign-offs above, then dispatch Task
1 as the first Executor call, followed by Task 2 (the spike) with an Evaluator
pass gating everything after it.
