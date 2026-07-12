# Verification Contract

Pulse Court is an original game. It cannot be truthfully verified against any
commercial title without licensed source material, a permitted behavior corpus,
and an agreed measurement protocol. This project instead verifies conformance
to the original Pulse Court ruleset.

## Automated tests (`pulse_tests`)

1. Fixed-point normalization, length cap, and overlap fallback behavior.
2. Same deterministic input tape gives the same state hash every tick for at
   least 100,000 ticks.
3. Restoring a saved state gives identical continuation hashes.
4. Mirroring a populated state twice restores the exact original state.
5. A core through a goal mouth scores exactly once and starts kickoff.
6. A core outside the goal mouth bounces from the side wall.
7. No core tunneling at maximum configured speed.
8. Strike one-hit behavior, dash cooldown, and score/match termination rules.
9. Each character ability activation, effect, cooldown, and expiry.
10. All 324 actions decode into valid bounded inputs.
11. Replay record/load/verify plus incompatible-version rejection.
12. Core travel is not accidentally multiplied by nested substeps, separating
    contacts do not reverse the core, a dash exceeds normal movement speed,
    and strikes ignore a core behind the striker.

## Runtime validation

- `pulse_headless` supports `--record`, `--replay`, `--verify`, and
  `--benchmark`.
- `pulse_viewer` links the same `pulse_core` library and renders with Win32/GDI.

## Verification labels

- **Compiled only** — the build gate passed; runtime behavior not established.
- **Unit-tested** — focused automated tests passed.
- **Viewer smoke-tested** — the visualizer launched and rendered the
  court/HUD. Reset/quit controls and live feel testing require a human player.
- **Deferred intentionally** — AI/training, additional team sizes, networking,
  gamepad support, audio, polish tuning, and commercial-game comparison.
