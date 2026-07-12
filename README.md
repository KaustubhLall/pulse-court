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

```cmd
build\release\Release\pulse_viewer.exe --left kite --right bastion
```

Controls:

| Left | Right | Global |
|------|-------|--------|
| Move: WASD | Move: IJKL (I=up, J=left, K=down, L=right) | Reset: R |
| Strike: F | Strike: U | Pause: Space |
| Ability: G | Ability: O | Quit: Esc |
| Dash: H | Dash: P | |

Aim defaults to the current movement direction; when movement is neutral the
player keeps its last facing.

The viewer uses a genuine 120 Hz fixed simulation accumulator with 60 FPS
rendering, displays the 10 Hz policy clock, and pauses when the window loses
focus or when manually paused. Strike, Ability, and Dash actions are edge-
triggered (one press per action) rather than held. The viewer includes an
action trace, policy inspector placeholder, and procedural animations for all
core events.

## Art assets

`assets/sprites/` contains an original transparent player/action sprite pack
for Kite, Vale, and Bastion, plus a core and ability-effects sheet. The
grid-based [sprite manifest](assets/sprites/manifest.json) makes the sheets
ready for a bitmap renderer without binding visual assets into the deterministic
simulation. The current Win32 viewer continues to use its procedural renderer;
sprite-sheet rendering is a presentation-only follow-up.

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
