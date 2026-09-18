"""Run bounded, non-certifying native-policy smoke and save portable evidence.

Uses only the Python standard library and built Release executables. Output
must be a new directory. Never reads training checkpoints or reserved seeds.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from datetime import datetime, timezone


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build", type=Path, default=Path("build/release/Release"))
    args = parser.parse_args()
    output = args.output.resolve()
    executables = args.build.resolve()
    headless = executables / "pulse_headless.exe"
    devgen = executables / "pulse_dev_bundle.exe"
    for exe in (headless, devgen):
        if not exe.is_file():
            raise RuntimeError(f"Build first: missing {exe}")
    output.mkdir(parents=True, exist_ok=False)
    commands = []

    def run(argv, expected=0):
        result = subprocess.run([str(x) for x in argv], capture_output=True,
                                text=True, timeout=120)
        portable = lambda s: s.replace(str(output), "<output>").replace(
            str(executables), "<build>")
        commands.append({"argv": [portable(str(x)) for x in argv],
                         "exit_code": result.returncode,
                         "stdout": portable(result.stdout),
                         "stderr": portable(result.stderr)})
        if result.returncode != expected:
            raise RuntimeError(f"Unexpected exit {result.returncode}: {result.stderr}")
        return result

    prefix = output / "synthetic dev"
    run([devgen, prefix])
    bundle = prefix.with_suffix(".pzpb")
    smoke = output / "preview smoke"
    run([headless, "--challenge-smoke", "--policy-bundle-preview", bundle,
         "--games", "6", "--output-dir", smoke])
    games = [json.loads(line) for line in
             (smoke / "games.jsonl").read_text().splitlines()]
    if len(games) != 6 or {g["bot_seat"] for g in games} != {0, 1}:
        raise RuntimeError("Smoke did not exercise six games and both seats")
    replay_results = []
    for game in games:
        if not (game["non_certified_preview"] and game["policy_mode"] == "preview"
                and game["network_decision_count"] > 0):
            raise RuntimeError("Smoke did not record preview network execution")
        replay = smoke / game["replay_path"]
        raw = replay.read_bytes()
        magic, version, ruleset = struct.unpack_from("<8sII", raw)
        ticks = struct.unpack_from("<I", raw, 30)[0]
        if magic != b"PULSERPL" or version != 1 or ruleset != 2:
            raise RuntimeError("Unexpected replay contract")
        if len(raw) != 34 + 18 * ticks or ticks == 0:
            raise RuntimeError("Unexpected replay length")
        final_hash = str(struct.unpack_from("<Q", raw, len(raw) - 8)[0])
        if final_hash != game["final_state_hash"]:
            raise RuntimeError("Replay and game evidence disagree")
        run([headless, "--replay", replay])
        replay_results.append({"game_index": game["game_index"],
                               "bot_seat": game["bot_seat"],
                               "ticks": ticks,
                               "network_decisions": game["network_decision_count"],
                               "final_state_hash": final_hash,
                               "sha256": hashlib.sha256(raw).hexdigest(),
                               "replay": replay.relative_to(output).as_posix(),
                               "verified": True})

    missing_output = output / "must not exist"
    failed = run([headless, "--challenge-smoke", "--policy-bundle",
                  output / "missing.pzpb", "--policy-manifest",
                  output / "missing.json", "--games", "6", "--output-dir",
                  missing_output], expected=1)
    if "strict policy load failed" not in failed.stderr:
        raise RuntimeError("Missing strict input failed for an unexpected reason")
    if missing_output.exists():
        raise RuntimeError("Missing strict input created session evidence")

    scripted = output / "scripted smoke"
    run([headless, "--challenge-smoke", "--scripted-policy", "--games", "6",
         "--output-dir", scripted])
    scripted_games = [json.loads(line) for line in
                      (scripted / "games.jsonl").read_text().splitlines()]
    if len(scripted_games) != 6 or any(
            g["policy_mode"] != "scripted" or g["network_decision_count"] != 0
            for g in scripted_games):
        raise RuntimeError("Explicit scripted smoke recorded network execution")
    report = {
        "observed_at_utc": datetime.now(timezone.utc).isoformat(),
        "scope": "synthetic untrained native-policy preview; not certification",
        "ruleset": 2,
        "trained_policy": False,
        "certified": False,
        "games_verified": len(replay_results),
        "network_decisions": sum(x["network_decisions"] for x in replay_results),
        "bundle_sha256": hashlib.sha256(bundle.read_bytes()).hexdigest(),
        "missing_strict_input_rejected": failed.returncode != 0,
        "strict_failure_created_evidence": False,
        "scripted_games": len(scripted_games),
        "scripted_network_decisions": 0,
        "replays": replay_results,
        "commands": commands,
    }
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items()
                      if k not in ("commands", "replays")}, indent=2))


if __name__ == "__main__":
    main()
