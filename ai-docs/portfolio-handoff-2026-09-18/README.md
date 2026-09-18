# Pulse Court portfolio handoff

**Status: verified development checkpoint; synthetic policy preview.**

Branch: `codex/preview-runtime-fixes`.
Final executable source commit: `661f35dba583d51bb33a32e398b3471b29b48e12`.
The launcher fix is included through ancestor `d453850`; audit checkpoint
`97c54e7` preserves the starting assessment. This folder is packaged by a
later documentation-only commit. Resolve its exact revision with
`git log -1 --format=%H -- ai-docs/portfolio-handoff-2026-09-18`.

## Suggested public description

> Built a deterministic arena game with native policy inference and
> independently verified replays. Added explicit controller modes, per-game
> inference evidence, simulator compatibility checks, and regression tests
> that compare recorded bot inputs against a separately stepped controller.

Optional measured sentence:

> A synthetic-policy smoke run exercised both seats across six games,
> recorded 9,483 network decisions, and verified every saved replay.

Use the word **synthetic** with these numbers. This is an untrained dev
bundle; the results demonstrate execution and replay integrity. They do not
measure learned skill, human performance, or certification. See
[claims.md](claims.md) for the source of every proposed number.

## What changed

- The viewer launcher preserves spaced paths, embedded quotes, and trailing
  backslashes, and resolves caller-relative paths before changing the child
  working directory. Windows PowerShell and PowerShell 7 argument-parser
  regressions passed; no interactive viewer test was performed this session.
- Strict policy loading now rejects this compiled ruleset-v2 simulator against
  the supported v4 policy contract, after preserving manifest schema checks.
- Strict smoke no longer discards its loaded policy or silently substitutes a
  scripted controller when files are missing. Scripted smoke uses an explicit
  flag. Network runs record mode and fresh decision count.
- Regression tests reproduce every recorded bot input and state hash with an
  independent controller/simulation, match the decision count, and establish
  that the network input stream differs from the scripted stream. These new
  parity checks execute in both Release and Debug.
- Project docs now describe the mixed D2D/GDI viewer and preview limitations.

## Verified evidence

- Release and Debug builds passed. Each configuration passed all four CTest
  suites; sanitized outputs are [release-tests.txt](release-tests.txt) and
  [debug-tests.txt](debug-tests.txt).
- [results.json](results.json) preserves the actual bounded run commands,
  exit codes, native verifier output, per-game hashes and network decisions.
  Six preview games produced 9,483 decisions. Six scripted control games
  produced zero network decisions. Missing strict inputs returned exit 1
  without creating session evidence.
- [synthetic-preview.pulse](synthetic-preview.pulse) is a standalone native
  replay from that run: game index 4, Vale on the scripted side and Bastion on
  the synthetic-policy side. It was chosen because it is the smallest replay;
  the synthetic policy lost. All six game results remain in the JSON.
- The included replay contains 5,720 ticks; its copy was independently passed
  through `pulse_headless --replay` and returned `5720 ticks OK`.
  SHA-256: `35af43287d7c8ae9f4e13d3103fc0f38d62d18b4d41d4a275534aff78c695f28`.

## Reproduce or play

From the repository root after building Release:

```powershell
# Headless verification of the included artifact; no model file required.
& 'build/release/Release/pulse_headless.exe' --replay 'ai-docs/portfolio-handoff-2026-09-18/synthetic-preview.pulse'

# Interactive native replay viewer (run by the user).
.\scripts\run_viewer.ps1 -Replay 'ai-docs/portfolio-handoff-2026-09-18/synthetic-preview.pulse'

# Regenerate bounded synthetic evidence in a NEW output directory.
python scripts/verify_preview_checkpoint.py --output 'build/new preview evidence'
```

The replay is the deliverable visual artifact for this checkpoint. No fresh
screenshot/GIF, graphical acceptance, or device-loss recovery claim is made.

## Remaining boundaries

- Strict trained-policy play still requires the actual ruleset-v4 integration,
  deployment parity/latency checks, canonical certification, and human testing.
- The separate local training clone, frozen scientific evidence, and held
  seeds were not modified or published. No A331 strength claim is made here.
- The self-play/previous-champion replay browser is unfinished.
- D2D gallery/setup rendering is implemented. Gameplay conversion, legacy GDI
  removal, visual/device-loss acceptance, and full-game performance evidence
  remain open.

This completes the bounded runtime-integrity and handoff task. Further feature
work should start as a separate scoped task.
