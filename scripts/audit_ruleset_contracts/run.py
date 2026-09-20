"""Build two isolated source probes; compare synthetic contracts, never models.

Uses only the Python standard library, CMake, and the existing C++ toolchain.
All generated files go to a NEW directory under the root build directory;
the separate reference clone and its frozen evidence are read-only inputs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from datetime import datetime, timezone


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake", default=shutil.which("cmake") or "C:/Program Files/CMake/bin/cmake.exe")
    args = parser.parse_args()
    tooling = Path(__file__).resolve().parent
    root = tooling.parent.parent
    reference = args.reference.resolve()
    output = args.output.resolve()
    if reference == root or not (reference / "src/core/pulse_sim.cpp").is_file():
        parser.error("reference must be a separate existing source tree")
    if not output.is_relative_to(root / "build") or output.is_relative_to(reference):
        parser.error("output must be under root/build and outside the reference tree")
    if output.exists():
        parser.error("output must be new; prior evidence is never overwritten")

    def git(tree: Path, *words: str) -> str:
        return subprocess.check_output(
            ["git", "-c", "safe.directory=" + tree.as_posix(), "-C", str(tree), *words],
            text=True, stderr=subprocess.PIPE, timeout=30,
        ).strip()

    files = ["src/core/" + name for name in
             ("fixed_math.cpp", "fixed_math.hpp", "pulse_sim.cpp", "pulse_sim.hpp", "pulse_replay.cpp", "pulse_replay.hpp")]
    files += ["src/ai/pulse_ai.cpp", "src/ai/pulse_ai.hpp"]

    def identity(tree: Path) -> dict:
        present = files + (["src/ai/pulse_core_compat.hpp"] if (tree / "src/ai/pulse_core_compat.hpp").exists() else [])
        return {"head": git(tree, "rev-parse", "HEAD"),
                "status_porcelain": git(tree, "status", "--porcelain"),
                "source_sha256": {name: sha(tree / name) for name in present}}

    before = {"v2": identity(root), "v4": identity(reference)}
    tools_before = {p.name: sha(p) for p in (tooling / "CMakeLists.txt", tooling / "probe.cpp", Path(__file__))}
    public_replay = root / "ai-docs/portfolio-handoff-2026-09-18/synthetic-preview.pulse"
    historical_sha = sha(public_replay)
    output.mkdir(parents=True)
    commands = []

    def sanitize(text: str) -> str:
        # Most-specific paths first; the reference and output are nested under root.
        for path, label in ((output, "<output>"), (reference, "<reference>"), (root, "<root>")):
            text = text.replace(str(path), label).replace(path.as_posix(), label)
        return text

    def run(argv: list[str], name: str, timeout: int = 120) -> str:
        print("Running " + name, flush=True)
        result = subprocess.run(argv, cwd=output, env=dict(os.environ), capture_output=True,
                                text=True, timeout=timeout)
        commands.append({"name": name, "argv": [sanitize(a) for a in argv],
                         "exit_code": result.returncode,
                         "stdout": sanitize(result.stdout), "stderr": sanitize(result.stderr)})
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n", encoding="utf-8")
        if result.returncode:
            raise RuntimeError(name + " failed: " + sanitize(result.stdout + result.stderr))
        return result.stdout

    build = output / "build"
    run([args.cmake, "-S", str(tooling), "-B", str(build), "-G", "Visual Studio 18 2026", "-A", "x64",
         "-DPULSE_ROOT=" + root.as_posix(), "-DPULSE_REFERENCE=" + reference.as_posix()], "configure")
    outputs = {}
    executable_hashes = {}
    replay_checks = []
    for config in ("Release", "Debug"):
        run([args.cmake, "--build", str(build), "--config", config], config + "-build", 300)
        for version in ("v2", "v4"):
            exe = build / config / ("audit_" + version + ".exe")
            key = config + "-" + version
            executable_hashes[key] = sha(exe)
            first = run([str(exe)], key + "-probe")
            second = run([str(exe)], key + "-repeat")
            if first != second:
                raise RuntimeError(key + " is not repeatable")
            outputs[key] = [json.loads(line) for line in first.splitlines()]
            (output / (key + ".jsonl")).write_text(first, encoding="utf-8")
        if config == "Release":
            for version in ("v2", "v4"):
                exe = build / config / ("audit_" + version + ".exe")
                run([str(exe), "--write-replay", str(output / (version + ".pulse"))], version + "-save-replay")
        for reader in ("v2", "v4"):
            exe = build / config / ("audit_" + reader + ".exe")
            for writer in ("v2", "v4", "historical_v2"):
                path = public_replay if writer == "historical_v2" else output / (writer + ".pulse")
                result = json.loads(run([str(exe), "--read-replay", str(path)], config + "-" + reader + "-read-" + writer))
                should_load = reader == writer or (reader == "v2" and writer == "historical_v2")
                if result["loaded"] != should_load or result["verified"] != should_load:
                    raise RuntimeError("Unexpected replay compatibility: " + str(result))
                if not should_load and result["error"] != "incompatible ruleset version":
                    raise RuntimeError("Unexpected replay rejection: " + str(result))
                replay_checks.append({"configuration": config, "reader": reader, "writer": writer, **result})
    for version in ("v2", "v4"):
        if outputs["Release-" + version] != outputs["Debug-" + version]:
            raise RuntimeError("Release/Debug probe divergence: " + version)
    left, right = outputs["Release-v2"], outputs["Release-v4"]
    if len(left) != len(right) or len(left[1]["values"]) != 648:
        raise RuntimeError("Incomplete probe output")
    differences = []
    observation_differences = []
    mask_mismatches = []
    for a, b in zip(left[2:], right[2:]):
        if (a["fixture"], a["stage"]) != (b["fixture"], b["stage"]):
            raise RuntimeError("Unmatched fixtures")
        key = {"fixture": a["fixture"], "stage": a["stage"]}
        for field in ("common", "touch_kind", "goal_result"):
            if a[field] != b[field]: differences.append({**key, "field": field, "v2": a[field], "v4": b[field]})
        for seat in range(2):
            if len(a["observation_bits"][seat]) != 64 or len(b["observation_bits"][seat]) != 64:
                raise RuntimeError("Observation width drift")
            for slot, (x, y) in enumerate(zip(a["observation_bits"][seat], b["observation_bits"][seat])):
                if x != y:
                    observation_differences.append({**key, "seat": seat, "slot": slot,
                        "v2": a["observation"][seat][slot], "v4": b["observation"][seat][slot], "v2_bits": x, "v4_bits": y})
            if a["masks"][seat] != b["masks"][seat]: mask_mismatches.append({**key, "seat": seat})
    after = {"v2": identity(root), "v4": identity(reference)}
    if before != after or tools_before != {name: sha(tooling / name) for name in tools_before}:
        raise RuntimeError("Source or repository status changed during audit")
    if sha(public_replay) != historical_sha:
        raise RuntimeError("Historical replay changed")
    action_mapping_equal = left[1] == right[1]
    metadata_differences = {key: {"v2": left[0][key], "v4": right[0][key]}
                            for key in left[0] if key not in ("type", "ruleset") and left[0][key] != right[0][key]}
    non_parity = bool(differences or observation_differences or mask_mismatches or
                      not action_mapping_equal or metadata_differences)
    result = {"schema_version": 1, "observed_utc": datetime.now(timezone.utc).isoformat(),
        "verdict": "NON_PARITY_ON_SYNTHETIC_FIXTURES" if non_parity else "NO_DIFFERENCE_ON_FIXTURES",
        "scope": "Hand-constructed contract probes; no policy/model loading, seeds, training, holdouts, or deployment",
        "source_identity": before, "audit_tooling_sha256": tools_before,
        "executable_sha256": executable_hashes, "metadata": {"v2": left[0], "v4": right[0]},
        "repeatability": {"two_runs_per_binary_exact": True, "release_debug_exact": True},
        "state_fixture_records": len(left) - 2, "action_cases": 648,
        "action_mapping_equal": action_mapping_equal, "metadata_differences": metadata_differences,
        "mask_cases": 2 * (len(left) - 2),
        "mask_mismatches": mask_mismatches, "observation_differences": observation_differences,
        "state_and_touch_differences": differences, "replay_checks": replay_checks,
        "historical_replay_sha256_unchanged": historical_sha, "reference_source_and_status_unchanged": True,
        "raw_hash_note": "Raw hashes are preserved, not used as semantic parity criteria: ruleset and touch-kind hash schema differ.",
        "limitations": ["Synthetic fixtures are diagnostic, not exhaustive state-space parity or trained-model certification.",
            "Probe links existing core/AI source directly; C ABI wrapper and policy inference are not executed.",
            "No authoritative simulation, policy, viewer, training, frozen artifact, or reference-clone files changed."]}
    (output / "results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("verdict", "state_fixture_records", "action_cases", "action_mapping_equal", "mask_cases", "mask_mismatches")}, indent=2))


if __name__ == "__main__":
    main()
