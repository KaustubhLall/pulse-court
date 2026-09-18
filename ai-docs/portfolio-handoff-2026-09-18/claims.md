# Public claim evidence

Executable source: `661f35dba583d51bb33a32e398b3471b29b48e12` on
`codex/preview-runtime-fixes`. Run evidence was generated from the same
production source before that commit was created; the final rebuild changed
only regression-test assertions. Dates in `results.json` are UTC.

| Proposed claim or number | Evidence | Limit |
| --- | --- | --- |
| Deterministic 1v1 arena simulation at 120 Hz | `src/core/pulse_sim.hpp` (`kSimHz`, two-player GameState); `tests/sim_tests.cpp`; replay verifier output in `results.json` | Original game rules; no claim of matching a commercial game |
| Six preview games, both seats | `results.json`: `games_verified=6`, `replays[].bot_seat`; recorded `--challenge-smoke --policy-bundle-preview` command | Synthetic, untrained policy against a scripted opponent |
| 9,483 native network decisions | Sum of `results.json` `replays[].network_decisions`; `src/ai/challenge_smoke.cpp` increments only on a fresh `PolicyController` decision; `test_smoke_preview_mode` in `tests/challenge_tests.cpp` independently matches recorded inputs/count | Execution evidence, not strength or inference-latency evidence |
| All six replays verified | Six `--replay` commands in `results.json`, each exit 0 and native `ticks OK` output; SHA-256 recorded per file | Only the smallest replay is packaged; regenerate the others using the checked-in script |
| Six scripted control games, zero network decisions | `results.json`: `scripted_games=6`, `scripted_network_decisions=0`; explicit `--scripted-policy` command/output | A controller-selection check, not an experimental baseline comparison |
| Four passing suites in Release and four in Debug | `release-tests.txt`, `debug-tests.txt`; commands `ctest --test-dir build/release -C Release --output-on-failure --timeout 90` and corresponding Debug command | No GUI or trained-model acceptance follows from these suite counts |
| Included replay has 5,720 ticks and 478 decisions | `results.json` game index 4; native verification of `synthetic-preview.pulse`; per-game controller count | Shortest artifact was selected for size; it is a loss by the synthetic policy |
| Strict runtime mismatch rejected | `src/ai/pulse_policy.cpp::validate_manifest`; `test_runtime_ruleset_gate_is_separate_from_schema_validation` | v2/v4 incompatibility is blocked; successful strict v4 play is not verified |
| Missing strict inputs fail before evidence creation | `results.json`: failing strict command returns exit 1; `strict_failure_created_evidence=false`; `scripts/verify_preview_checkpoint.py` checks directory absence | Does not substitute for valid-artifact certification/parity |
| Windows spaced and relative paths preserved | `scripts/tests/test_run_viewer_arguments.ps1`, passed under `powershell.exe` and `pwsh`; native `CommandLineToArgvW` round-trip | Launcher/parser verification; viewer visuals were not retested |

No public claims of a certified model, superhuman play, a human win rate,
completed self-play browser, completed D2D gameplay migration, new rendering
FPS, or deployment p99 latency are supported by this handoff. The historical
training A/B scores and spike timings must not be reused as current release
results.
