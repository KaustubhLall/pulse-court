# Pulse Court

A deterministic, headless-first 1v1 arena-striker foundation. This first
vertical slice provides the authoritative simulation, replay system, headless
benchmark/verification tool, and a native Win32/GDI viewer. It intentionally
does **not** contain AI, neural nets, bots, networking, or 2v2/3v3 gameplay.

Pulse Court is an original project. It is not affiliated with Omega Strikers
or Odyssey Interactive, and it uses no copied characters, maps, assets,
ability names, or commercial-game tuning.

Characters: **Kite** uses Jetstep for a short speed burst and powered strike;
**Vale** places an Anchor Well that bends the core; **Bastion** deploys a
core-reflecting Pulse Gate.

## Current ruleset (v2)

- Court: 38 by 22 world units, with goal mouths centered on the two short sides
  between y=8 and y=14.
- Simulation: 120 Hz fixed tick rate with deterministic integer/fixed-point physics.
- Decision cadence: 10 Hz (every 12 simulation ticks) for future AI policy refresh;
  human/viewer input is not throttled.
- Event stream: optional fixed-capacity (16 events) non-authoritative telemetry for
  visualizers; does not affect state, replay, or hashing.

## Build

Requires C++20, CMake, and Visual Studio 2026 (x64). CMake is configured to
find the installed Visual Studio generator.

Configure and build Release:

```cmd
cmake --preset vs-x64-release
cmake --build build/release --config Release
```

Or with explicit generator selection:

```cmd
cmake -B build/release -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --config Release
```

## Test

```cmd
cd build/release
ctest -C Release --output-on-failure
```

Or run the test executable directly:

```cmd
build\release\Release\pulse_tests.exe
```

## Headless usage

```cmd
build\release\Release\pulse_headless.exe --ticks 12000 --left kite --right bastion
build\release\Release\pulse_headless.exe --record game.pulse --left kite --right bastion --verify
build\release\Release\pulse_headless.exe --replay game.pulse
build\release\Release\pulse_headless.exe --benchmark 100000 --left kite --right bastion
build\release\Release\pulse_headless.exe --ticks 10000 --hash-interval 1000
```

## Viewer

### Canonical launch

The canonical way to launch the viewer is the `scripts/run_viewer.ps1` wrapper.
It validates the build and assets, defaults to the setup screen, and forwards
optional flags.

```powershell
# Normal setup screen
.\scripts\run_viewer.ps1

# Observer-only window on the Sceptre F27 display
.\scripts\run_viewer.ps1 -Monitor scepter -Passive
```

`-Passive` is **observer-only**: the window does not activate or steal focus.
It is intended for visual QA and screenshot capture, not for normal
interactive play.

### Direct launch

For automated smoke tests, the executable can be launched directly with
`--left` and `--right`:

```cmd
build\release\Release\pulse_viewer.exe --left kite --right bastion
```

### Setup screen

```cmd
build\release\Release\pulse_viewer.exe --setup
```

### Sprite gallery

```cmd
build\release\Release\pulse_viewer.exe --gallery
```

In gallery mode, the viewer shows one loaded sprite sheet at a time. Use
`Up`/`Down` to switch sheets, `Left`/`Right` to switch frames, and `Esc` to quit.

Running `pulse_viewer.exe` with no arguments, with `--setup`, or without both
`--left` and `--right` opens the character-selection screen. Each player can
pick Kite, Vale, or Bastion using the mouse or keyboard:

| Action | Key |
|--------|-----|
| Switch active player | Tab |
| Previous / next hero | Left / Right |
| Start match | Enter |
| Quit | Esc |

Mouse clicks on a card select that hero for that player. Press **Start Match**
(or Enter) to begin.

### In-match controls

| Left | Right | Global |
|------|-------|--------|
| Move: WASD | Move: IJKL (I=up, J=left, K=down, L=right) | Reset: R |
| Strike: F | Strike: U | Pause: Space |
| Ability: G | Ability: O | Quit: Esc |
| Dash: H | Dash: P | Step: N (while paused) |
| | | Hitbox overlay: F1 |
| | | Animation debug: F2 |
| | | Bot P2: B |
| | | Sim speed: `[` / `]` |

Aim defaults to the current movement direction; when movement is neutral the
player keeps its last facing.

The viewer uses a genuine 120 Hz fixed simulation accumulator with 60 FPS
rendering, displays the 10 Hz policy clock, and pauses when the window loses
focus or when manually paused. Strike, Ability, and Dash actions are edge-
triggered (one press per action) rather than held. The viewer includes an
action trace, policy inspector placeholder, and sprite-based animation for all
core events with procedural fallback for any missing art.

## Art assets

`assets/sprites/animated/` contains the converted transparent player/action
sprite pack for Kite, Vale, and Bastion, the core, ability effects, impacts,
goal, kickoff, ability icons, and arena art. The grid-based
[sprite manifest](assets/sprites/animated/manifest.json) describes the runtime
paths, column counts, row counts, and frame conventions used by the viewer.
`file` values are relative to the `assets/` directory (sprite sheets live under
`sprites/animated/`; `arena.png` lives at the `assets/` root).

The viewer loads PNGs via GDI+ once at startup. Missing or invalid assets fall
back to the procedural renderer (colored circles, lines, goal flash, etc.) and
the footer prints status text for the missing item. The assets are not linked
into the headless build or tests.

## Desktop shortcut

The build produces `pulse_viewer.exe` and a sibling `assets` directory. The
shortcut script defaults to `build\release\Release` relative to the repository
root. To add a desktop shortcut:

```powershell
.\scripts\create_desktop_shortcut.ps1
```

By default the script creates `Pulse Court.lnk` on the current user's Desktop
with `--setup` as the argument and the `assets` icon. To test in a temporary
location, pass `-OutputPath` (quote the path if it contains spaces):

```powershell
.\scripts\create_desktop_shortcut.ps1 -OutputPath "C:\Temp\My Shortcut\Pulse Court.lnk"
```

To point the shortcut at a different build directory, use `-BaseDir`:

```powershell
.\scripts\create_desktop_shortcut.ps1 -BaseDir "C:\Path\To\Pulse Court\build\release\Release"
```

The script validates the viewer executable and `assets` directory before
creating or overwriting the shortcut.

> **Deprecation note:** old isolated build folders such as `build\visual-*` are
> disposable local QA artifacts and are not a supported launch surface. The
> canonical build path is `build\release` and the canonical launcher is
> `scripts\run_viewer.ps1`.

## Verification labels

- **Compiled only** — initial configure/build gate.
- **Unit-tested** — `pulse_tests` covering fixed math, determinism, state
  restore, mirroring, goals, wall bounces, tunneling, strike/dash, abilities,
  action space, replay, court geometry, decision boundaries, action events,
  strike hits, core bounces, goal events, and event non-authoritativeness.
- **Viewer smoke-tested** — `pulse_viewer.exe` builds and renders the court
  and sidebar in a live Win32 window. Reset/quit controls are implemented but
  still need a human keyboard pass; feel balancing remains separate.
- **Deferred intentionally** — AI/training, additional team sizes, networking,
  gamepad support, audio, polish tuning, and comparison with any commercial
  game.

## Documentation

- `docs/rules.md` — original Pulse Court ruleset.
- `docs/architecture.md` — determinism, resolution order, and state contract.
- `docs/verification.md` — verification target and labels.
- `docs/replay-format.md` — binary replay file layout.
