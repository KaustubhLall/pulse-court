# Pulse Court Architecture

## Core philosophy

The authoritative simulation is fully deterministic, headless-first, and uses
only integer/fixed-point arithmetic. Rendering, windowing, audio, wall-clock
time, filesystem I/O, randomness, threads, and external packages are kept out
of `pulse_core`.

## Fixed-point representation

- All positions, velocities, cooldowns, and tuning constants use `std::int32_t`
  values with `kFixedScale = 1024` (one world unit == 1024 fixed units).
- Multiplications use `std::int64_t` intermediates to prevent overflow.
- Vector normalization uses a 64-bit integer square-root and keeps the result
  scaled by `kFixedScale`.

## Coordinate system

- Court: 32 x 18 world units, fixed to `32768 x 18432`.
- Origin is the bottom-left corner; `+y` is up.
- Goal mouths are on the left and right walls between `y=6` and `y=12`.
- Left team (index 0) defends the left goal and attacks right.
- Right team (index 1) defends the right goal and attacks left.

## Tick flow

1. Increment tick and decrement all timers.
2. If `MatchOver`, return immediately.
3. If `Kickoff`, count down `kickoff_remaining` and return when it reaches 0.
4. Apply inputs for each player in fixed order (0 then 1).
5. Resolve player motion: acceleration/friction, speed cap, wall clamp, then
   player-vs-player collision.
6. Resolve strikes: each swing can hit the core at most once.
7. Apply effect forces (Anchor Well pull).
8. Advance the core with `kCoreSubsteps = 2` substeps per tick:
   - integrate position,
   - resolve active Pulse Gates,
   - resolve player contacts in fixed order,
   - detect goals before wall reflection,
   - reflect off non-goal walls/top/bottom.
9. If a goal was scored, update score and either finish the match or enter a
   90-tick `Kickoff` reset.
10. Update the regulation clock; tied expiry enters golden goal.

## Determinism guarantees

- Fixed 120 Hz tick rate with no wall-clock dependence.
- No allocation inside `Simulation::step`.
- All resolution order is explicit: players 0 then 1, gates before players,
  goals before wall bounces.
- Exact-overlap fallbacks are deterministic and documented in the source (push
  opposite core velocity, or separate players along the x-axis in index order).
- `state_hash` is a stable FNV-1a over every authoritative field, including the
  embedded `MatchConfig`.

## State capacity

The current `GameState` deliberately stores two players because this is a 1v1
vertical slice. Moving to 2v2 or 3v3 requires expanding the fixed player array,
input frame, collision loop, observation contract, and tests together; it is
an intentional deferred feature, not a claimed capability.

## Replay contract

- Replays carry their own `format_version`, `ruleset_version`, character
  selection, and `MatchConfig`.
- `load_replay` rejects unknown magic, format versions, or ruleset versions.
- Verification replays ticks through a fresh simulation and compares state
  hashes; the first mismatch is reported with tick, expected, and actual hash.

## Boundaries

- `pulse_core` (library) has no OS, window, or rendering dependencies.
- `pulse_headless` is a command-line harness with a deterministic scripted
  input tape; it contains no AI.
- `pulse_viewer` is a Win32/GDI visualizer that links only `pulse_core`,
  `user32`, `gdi32`. It may render at any frame rate but advances the
  simulation one fixed tick at a time.
