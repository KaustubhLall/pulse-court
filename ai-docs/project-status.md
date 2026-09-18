# Pulse Court project status

## Audit checkpoint — 2026-09-18

Base: `454dd6b` on the local `d2d-migration` branch. This audit precedes the
bounded launcher/policy fixes on `codex/preview-runtime-fixes`.

The root working tree was clean and contained 17 commits beyond GitHub main
(`a656104`). The D2D and challenge changes had not been pushed. GitHub had no
open or historical PRs; a live Linear search found no matching project or
issues. The separate `build/pulse-zero-work` clone contains additional local
commits and uncommitted training/deployment work that a root-repository push
does not include.

### Implemented before this checkpoint

- Deterministic ruleset-v2 simulation, native viewer, and hash-verified replay.
- GDI performance work and D2D spike, gallery, and setup rendering.
- Native FP32 policy inference, explicit non-certified preview loading,
  challenge logging, per-game replays, and viewer challenge/replay modes.

### Verified during the audit

- Release and Debug builds, including the viewer and headless targets.
- Four CTest suites passed in each configuration.
- A 100,000-tick Kite/Bastion headless benchmark completed with final state
  hash `13227077027835972081`.
- No new GUI, input, rendering, or device-loss acceptance was performed.

### First bounded fix

1. Preserve spaced paths passed through `scripts/run_viewer.ps1`.
2. Reject strict policy loading when compiled simulator rules differ from the
   policy contract. The existing schema check compares against literal v4,
   while this checkout's simulator is v2.
3. Retain the loaded strict policy through headless smoke execution. The
   current strict branch loads metadata but runs a scripted controller.
4. Refresh the stale README, architecture, replay, verification, and D2D status.

Acceptance requires focused regressions, Release/Debug tests, and replay/input
evidence of real loaded-policy execution. Passing tests alone must not be
presented as trained-model strength or certified compatibility.

### Deferred work

- Ruleset-v4 simulator/adapter/replay integration and canonical deployment
  acceptance. The compatibility shim loses touch-kind information.
- A331 held certification and a complete human challenge. The earlier frequent
  A/B report rejected treatment B; A331 remains a candidate.
- The requested self-play versus earlier champions and replay-browser UI.
  The implementation brief exists locally; the browser implementation was not
  found in the examined source.
- D2D gameplay conversion, legacy GDI removal, and measured sprite-pipeline
  decision. Gallery/setup cutovers are implemented, but gameplay remains GDI.
- Preservation of the separate clone and ignored scientific evidence.

The phase-4 continuation files were a byte-identical copy of phase 2 at audit
time; no later training update was established. Its provenance note records a
pending DLL substitution decision. Frozen phase-2 data and held seeds remain
out of scope for this checkpoint.

The historical DLL adjudication report is present outside the clone under
`build/pulse-zero-dll-equivalence/`. Its SHA-256 matched the frozen plan:
`61fd6e86d6702d998a585d2c0bd50304dfbd8a64abc223b1770a339432e6993a`.
It reports equality of the saved observable projection with a checksum
correction; it does not establish certified policy deployment.
