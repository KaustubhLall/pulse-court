# Ruleset v2/v4 contract audit — 2026-09-20

**Verdict: verified non-parity on synthetic fixtures.** The version guard is
protecting real observation, physics, and touch-provenance differences.
This audit changes no authoritative behavior and makes no trained-strength or
certification claim. No push, publication, or tracker write was performed.

Audit tooling commit: `0e28c2217590c05776e059fd6af9ef0ff0c7481a`.
Compiled root inputs: `3a3fe53030fb71e60f26c0e969462e825985bffd`.
Compiled reference inputs: `9a5dfef3cecf82ca985425852036c3fdc4bbefe5`, in the
separate `build/pulse-zero-work` clone. That clone has unrelated dirty work;
every compiled input matched its committed HEAD after line-ending normalization.
Exact raw-file hashes, repository status snapshots, executable hashes, and
review results are preserved in [results.json](results.json).

## Measured gaps

The same C++ probe was compiled separately against each tree's existing core
and observation encoder. Fixtures are hand-constructed; contact/goal cases
adapt the reference's existing simulation tests. Numbers below are direct
outputs, in fixed-point units where applicable.

| Probe | Root v2 | Reference v4 | Meaning |
| --- | --- | --- | --- |
| Identical core velocity `(160, -80)`, observation slots 2/3 before stepping | `(0.421052635, -0.210526317)` | `(0.5, -0.25)` | Normalization uses different speed caps, 380 versus 320 |
| Free core starts at vx 350; one tick | x 19806, vx 349 | x 19776, vx 319 | Cap change alters actual motion |
| Ordinary strike; one tick | vx 209, seat 0, kind PlayerBody via shim | vx 179, seat 0, kind Strike | Strike impulse and touch-kind divergence |
| Jetstep strike; one tick | vx 299 | vx 254 | Strike impulse plus bonus differ |
| Player-body contact; one tick | vx 174, seat -1, kind None | vx 174, seat 0, kind PlayerBody | Matching physical projection, lost ownership/type in v2 |
| AnchorWell pull; one tick | vx -7, seat -1, kind None | vx -7, seat 0, kind AnchorWell | Matching physical projection, lost ownership/type in v2 |
| PulseGate bounce; one tick | vx -261, seat -1, kind None | vx -261, seat 0, kind PulseGate | Matching physical projection, lost ownership/type in v2 |
| Nonwinning goal followed by reset | Goal shim yields seat -1 / None | StepResult captures seat 0 / Strike | Live state resets touch in both; v4 preserves pre-reset goal provenance |

The four source constants are core maximum speed **380/320**, strike impulse
**210/180**, minimum forward strike speed **155/135**, and Kite strike bonus
**90/75** (v2/v4). Minimum-forward enforcement was not separately isolated by
a fixture; its value is source/runtime-constant evidence only.

Observation width remains 64 floats and agent contract version remains 2.
Equal width is insufficient: ownership slots 12–14, touch-kind slots 15–19,
and velocity normalization differ. Five synthetic prior-touch states further
isolate the kind encoding. The root cannot represent the v4 kind field, so
those cases explicitly test the lossy shim rather than claiming identical
complete internal states.

## Matching behavior and verification

- **648 action mappings matched:** all 324 `Simulation::decode_action` indices,
  each as physical seat 0 and horizontally mirrored seat 1. The policy adapter
  source is byte-identical across trees, but the adapter/inference path and C
  ABI wrapper were not executed in this audit.
- **80 mask comparisons matched:** both seats across 40 state snapshots. These
  include every combination of four strike/cooldown flags plus kickoff and
  match-over cases. These are fixture comparisons, not an exhaustive proof over
  all possible states.
- **32 fixtures produced 40 snapshots**, including eight one-tick transitions.
  There were 78 differing observation seat/fixture/slot entries and 16 differing
  state/touch field records. These counts describe diagnostics, not a success
  rate or model score.
- Release and Debug audit binaries built successfully. Two runs of every
  binary matched exactly, and Release/Debug outputs matched for each ruleset.
  No production sources changed, so this audit did not rerun the full project
  suites already recorded in the replay-admission handoff.
- **12 replay checks matched the expected boundary:** generated one-tick v2/v4
  replays verify with their own ruleset and reject the other ruleset. The saved
  historical v2 replay verifies at 5,720 ticks in the v2 probe and is rejected
  by v4. Its SHA-256 remained
  `35af43287d7c8ae9f4e13d3103fc0f38d62d18b4d41d4a275534aff78c695f28`.
- Independent Astra low read-only review checked the source and raw results.
  It caught an incomplete verdict predicate; the corrected final run includes
  mask/action/metadata differences as well as state/observation differences.

The neutral control has identical common state and observations. Raw state
hashes still differ because they include ruleset identity and v4 adds a touch
kind field. Raw hashes are retained for traceability and **are not used as
cross-ruleset semantic parity criteria**.

## Evidence and reproduction

- [results.json](results.json): full differences, identities, limits, and replay outcomes.
- [commands.json](commands.json): 25 actual commands, exit codes, build output,
  and native replay diagnostics. Repeated probe stdout is represented once per
  ruleset, with its UTF-8 checksum and artifact link to avoid duplication.
- [v2-probe.jsonl](v2-probe.jsonl), [v4-probe.jsonl](v4-probe.jsonl): complete
  decoded mappings and raw state, observation, bit-pattern, and mask records.

```powershell
# From the root repository; use a NEW output directory.
python scripts/audit_ruleset_contracts/run.py --reference build/pulse-zero-work --output build/new-ruleset-audit
```

Requires the existing CMake/Visual Studio 2026 toolchain and Python's standard
library. The script compiles selected source files directly using its own small
CMake project; it does not execute the reference's modified CMake configuration.
All generated files stay in the new root build directory, outside the reference
clone. It compares source hashes and repository status before/after execution.
No models, scenario seeds, frozen evaluation files, or training code are loaded.
Reproduction requires the identified reference source tree; the separate clone
is not published or copied into this package.

Raw record conventions: `common.core` is `[x,y,vx,vy]`; `clock` is
`[tick,phase,kickoff_remaining,regulation_remaining,golden_goal,score0,score1]`;
`goal_result` is `[scored,team,finished,touch_seat,touch_kind]`. Kind codes are
None 0, Strike 1, PlayerBody 2, AnchorWell 3, PulseGate 4. `common.players`
contains character, position x/y, velocity x/y, facing x/y, effect position
x/y, effect kind, two flags (bonus used/hit), strike ticks, strike/dash/ability
cooldowns, and effect ticks. Observation and mask outer indices are physical
seats; observations themselves are perspective-canonical. Float bit arrays
support exact comparison. Action arrays contain seat-0 indices 0–323 followed
by mirrored seat-1 indices 0–323.

## Existing tooling and integration recommendation

Existing Python/native golden-case tooling tests bundle inference, while the
archived DLL adjudication compares historical v4 replacement-DLL observables.
Neither establishes root-v2 versus reference-v4 environment parity. They were
inspected and identified by hash, not rerun. Existing touch fixtures were reused
instead of selecting any training or holdout cases.

**Next step requires a scoped core migration, not a version-number edit.**

1. Port the identified v4 physics constants, authoritative touch-kind state,
   actual contact ownership updates, pre-reset goal provenance, mirroring, and
   hashing as one coherent simulator contract; replace the lossy AI shim with
   those authoritative fields. Keep strict policy loading blocked meanwhile.
2. Preserve the new replay extent/transactional-load protections. Do not copy
   the older reference replay loader wholesale. Define historical v2 replay
   support explicitly (retained v2 verifier/runtime or versioned backend);
   rewriting headers or comparing hashes across versions is not conversion.
3. Rerun these probes, extend minimum-forward-speed and multi-tick coverage,
   then perform separate environment-adapter and policy golden parity against
   identified artifacts under an authorized integration plan. Certification,
   latency acceptance, and human play remain distinct later gates.

Work stops here: no core integration, guard bypass, clone merge, training,
fresh holdout evaluation, model loading, visual acceptance, or deployment was
performed. The fixture evidence cannot establish trained strength or v4
certification.
