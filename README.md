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

| Left | Right |
|------|-------|
| Move: WASD | Move: Arrow keys |
| Strike: F | Strike: Numpad 1 |
| Ability: G | Ability: Numpad 2 |
| Dash: H | Dash: Numpad 3 |
| Reset: R | |
| Quit: Esc | |

Aim defaults to the current movement direction; when movement is neutral the
player keeps its last facing.

## Verification labels

- **Compiled only** — initial configure/build gate.
- **Unit-tested** — `pulse_tests` covering fixed math, determinism, state
  restore, mirroring, goals, wall bounces, tunneling, strike/dash, abilities,
  action space, and replay.
- **Viewer smoke-tested** — `pulse_viewer.exe` builds and renders the court
  and HUD in a live Win32 window. Reset/quit controls are implemented but
  still need a human keyboard pass; feel balancing remains separate.
- **Deferred intentionally** — AI/training, additional team sizes, networking,
  gamepad support, audio, polish tuning, and comparison with any commercial
  game.

## Documentation

- `docs/rules.md` — original Pulse Court ruleset.
- `docs/architecture.md` — determinism, resolution order, and state contract.
- `docs/verification.md` — verification target and labels.
- `docs/replay-format.md` — binary replay file layout.
