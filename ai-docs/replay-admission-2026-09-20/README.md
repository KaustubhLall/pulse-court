# Replay admission repair — 2026-09-20

Status: **PASS**, local source commit
`596d1f5e8d139bfb39c981e2296f0e8430a7e889` on
`codex/preview-runtime-fixes`. No push or deployment performed.

## Change

The loader now requires exactly `34 + 18 * tick_count` bytes before allocating
tick storage, with the uint32 count widened before multiplication. It reports
truncated headers, input frames, hashes, and trailing bytes. Parsing commits a
temporary replay only after success; failures preserve the complete caller
destination and success clears a stale diagnostic. Format v1 and ruleset v2
remain unchanged. Simulation physics, policy, and viewer code were not changed.

## Evidence

- Both configurations built successfully and passed all four CTest suites:
  [Release](release-tests.txt) and [Debug](debug-tests.txt).
- Always-active regression checks exhaust all 70 proper prefixes of a two-tick
  replay, three large forged counts in 34-byte files (including UINT32_MAX),
  trailing bytes, invalid magic/versions and missing files, preservation of a
  populated destination, recovery after failure, a byte-identical load/save
  roundtrip, and zero-tick admission.
- Fourteen native checks passed: each rebuilt Release/Debug headless executable
  accepted the historical replay and a zero-tick fixture and rejected five
  malformed cases (trailing byte, truncated header/frame/hash, forged maximum
  count). All actual commands, outputs, exit statuses, source/test identities,
  and executable SHA-256 values are in [verification.json](verification.json).
- A safe before probe showed that the preexisting executable accepted a replay
  with one appended byte. That binary's hash is recorded, but it was not rebuilt
  from the baseline commit for this probe. No huge-count fixture ran against
  the old loader.
- Independent Astra read-only review reported no material findings. The
  portfolio coordinator also inspected the loader diff. Their separate runtime
  checks are owned by that task; they are not counted in this handoff's results.

The public artifact inspected read-only was
`<portfolio>/public/data/pulse-court/synthetic-preview.pulse`: 102,994 bytes,
5,720 ticks, SHA-256
`35af43287d7c8ae9f4e13d3103fc0f38d62d18b4d41d4a275534aff78c695f28`
before and after verification. It matches the existing repository handoff copy
byte-for-byte. No public file was modified.

## Reproduce

From the repository root, build serially and run the existing suites:

```powershell
cmake --build build/release --config Release
ctest --test-dir build/release -C Release --output-on-failure --timeout 90
cmake --build build/debug --config Debug
ctest --test-dir build/debug -C Debug --output-on-failure --timeout 90
& 'build/release/Release/pulse_headless.exe' --replay 'ai-docs/portfolio-handoff-2026-09-18/synthetic-preview.pulse'
```

The JSON uses `<repo>`, `<portfolio>`, and `<cmake>` placeholders to sanitize
machine paths. Malformed native fixtures were created only in ignored
`build/replay-admission-2026-09-20`; the regression suite generates its own
bounded fixtures. Build subprocesses used a Python environment dictionary to
avoid duplicate `Path`/`PATH` keys. No machine settings changed. Existing
nodiscard/conversion warnings remain in unchanged helper/test code;
`clang-tidy` and `cppcheck` were unavailable in PATH.

## Limits and next action

Structural admission is separate from simulation/hash verification. This repair
adds no semantic input validator, file-size quota, allocation-exception policy,
or snapshot guarantee for concurrently modified files. It establishes no model
strength, ruleset-v4 certification, training result, or visual acceptance.

Next action: the portfolio coordinator can consume this local source identity
and bounded evidence after its independent runtime verification. Publishing or
pushing this repair remains outside the current authorization.
