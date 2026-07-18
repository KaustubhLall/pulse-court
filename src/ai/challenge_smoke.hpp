#pragma once

// ===========================================================================
// T4 -- headless --challenge-smoke policy semantics.
//
// Two distinct modes, selected by which CLI flags are present
// (see challenge_cli.hpp / ChallengeHeadlessArgs):
//
// 1. Synthetic scripted mode (current/default form):
//    `--challenge-smoke --policy-bundle X --policy-manifest Y --games 6
//    --output-dir DIR`. The bot seat is driven entirely by
//    ScriptedPolicyController (no network, no bundle bytes touched) --
//    this is the historical smoke-test behavior and its outputs/exit codes
//    are unchanged. As of T4, if `X` (the --policy-bundle value) exists on
//    disk as a regular file, headless_main additionally attempts a *strict*
//    pulse::Policy::load(X, Y) before running games. That loader fails
//    closed on any manifest/ruleset/provenance mismatch and prints its own
//    diagnostic to stderr; on failure headless_main exits 1 without running
//    any games. If `X` does not exist as a file (the synthetic/dummy-string
//    usage every existing test and the default synthetic PolicyMetadata in
//    headless_main relies on), this strict-load attempt is skipped entirely
//    and behavior is byte-for-byte identical to pre-T4.
//
// 2. Non-certified preview mode (new):
//    `--challenge-smoke --policy-bundle-preview PZPB --games 6
//    --output-dir DIR` (mutually exclusive with --policy-bundle; see
//    challenge_cli.hpp for the exact parse/validation rules). The bundle is
//    loaded via the *non-strict* preview path (pulse::Policy::load_preview /
//    load_from_bytes) -- no manifest, no provenance, no certification gate.
//    The bot seat is driven by pulse::ai::PolicyController wrapping that
//    loaded pulse::Policy (real 12-tick-cadence network inference instead of
//    the scripted tape). Every game evidence line this mode writes to
//    games.jsonl carries `"non_certified_preview":true`, and the headless
//    stdout summary is tagged the same way, so this mode's output can never
//    be mistaken for certified challenge evidence.
//
// run_smoke_game() below implements both modes through one function: pass
// `preview_policy == nullptr` for scripted mode (the historical behavior,
// existing call sites/tests unaffected since it defaults to nullptr), or a
// pointer to a loaded pulse::Policy for preview mode. GameEvidence::
// non_certified_preview is set to (preview_policy != nullptr).
// ===========================================================================

#include "challenge_session.hpp"
#include "pulse_sim.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace pulse {

// Forward declaration only -- avoids pulling the full policy network header
// (and its dependency on pulse_ai/pulse_policy_adapter) into every scripted-
// mode-only translation unit that includes this header.
class Policy;

// Decode a scripted-tape direction index into an integer move vector.
[[nodiscard]] std::pair<std::int8_t, std::int8_t> decode_script_dir(
    std::int32_t d);

// Deterministic scripted input tape. No bot, no search, no model.
[[nodiscard]] FrameInput scripted_input(std::uint32_t tick, std::size_t player,
                                        Character character);

// Runs a single smoke game to completion (or the smoke tick cap), recording
// a replay file to output_dir / "game_NNN.pulse" and filling out GameEvidence.
// Returns false and sets error on failure (e.g. replay save failure).
//
// `preview_policy`: nullptr (default) keeps the historical scripted-bot
// behavior (ScriptedPolicyController drives the bot seat; `policy` is used
// only for evidence metadata, no network involved). When non-null, the bot
// seat is instead driven by pulse::ai::PolicyController wrapping
// *preview_policy, and out.non_certified_preview is set to true.
[[nodiscard]] bool run_smoke_game(const PolicyMetadata& policy, const GameSchedule& gs,
                                  std::int32_t game_index,
                                  const std::filesystem::path& output_dir,
                                  GameEvidence& out, std::string& error,
                                  const Policy* preview_policy = nullptr);

}  // namespace pulse
