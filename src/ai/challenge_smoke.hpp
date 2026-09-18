#pragma once

// ===========================================================================
// Headless --challenge-smoke policy semantics.
//
// Three explicit modes are selected by ChallengeHeadlessArgs:
//
// 1. Scripted: --scripted-policy selects the historical deterministic tape.
// 2. Preview: --policy-bundle-preview loads one non-certified PZPB and drives
//    the bot with the native PolicyController cadence.
// 3. Strict: --policy-bundle plus --policy-manifest requires a strict,
//    provenance-bearing Policy load and drives the same native controller.
//
// Missing or invalid strict files fail closed; they are never converted into
// scripted games. Preview evidence is explicitly marked non-certified and all
// evidence records the mode and fresh native-network decision count.
//
// run_smoke_game() below implements all supported modes through one explicit
// function. Scripted mode uses the historical deterministic controller;
// preview/strict modes require a loaded pulse::Policy and use native
// PolicyController inference. GameEvidence carries the mode, decision count,
// and non-certified marker so preview output cannot be mistaken for certified
// challenge evidence.
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

enum class SmokePolicyMode {
    Scripted,
    Preview,
    Strict,
};

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
// The mode is explicit: Scripted requires a null loaded_policy; Preview and
// Strict require a non-null, loaded Policy and drive the bot through the real
// PolicyController cadence. Preview evidence is marked non-certified.
[[nodiscard]] bool run_smoke_game(const PolicyMetadata& policy, const GameSchedule& gs,
                                  std::int32_t game_index,
                                  const std::filesystem::path& output_dir,
                                  GameEvidence& out, std::string& error,
                                  SmokePolicyMode mode,
                                  const Policy* loaded_policy = nullptr);

}  // namespace pulse
