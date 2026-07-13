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
```

> **Deprecation note:** old isolated build folders such as `build\visual-*` are
> disposable local QA artifacts and are not a supported launch surface. The
> canonical build path is `build\release` and the canonical launcher is
> `scripts\run_viewer.ps1`.

## Viewer Assets

The Win32/GDI viewer loads PNGs from the `assets/` directory that is copied
next to `pulse_viewer.exe` by a post-build CMake step. `src/apps/viewer_assets.cpp`
contains the runtime `SheetId`/`kSheets` table; `assets/sprites/animated/manifest.json`
documents the same file paths and frame conventions.

## Key Scope Boundaries

- `src/core/` is the deterministic simulation; avoid viewer-only changes there.
- `src/apps/viewer_win32.cpp` owns the window, input, layout, and render loop.
- `src/apps/viewer_assets.cpp` owns GDI+ sprite loading/drawing and the
  procedural fallback.
- `src/apps/viewer_monitor.cpp` owns monitor enumeration and placement.

## Notes

- The viewer uses a 120 Hz fixed simulation accumulator and a 60 FPS render
  loop.
- Action inputs (Strike/Ability/Dash) are edge-triggered; movement/aim are
  continuous from the keyboard state.
- Window focus loss clears the keyboard state, pending input, and simulation
  input to prevent stuck keys.
- QA/debug controls: `Space` pause, `N` step one sim tick while paused, `[` and
  `]` cycle sim speed (0.25x / 0.5x / 1.0x), `F1` hitbox overlay, `F2` animation
  debug overlay, `F3` pseudo-3D compression toggle, `B` toggle deterministic P2
  bot.
- `--gallery` launches a sprite-sheet gallery for inspecting every loaded sheet
  and frame.
