#pragma once

#include "challenge_session.hpp"
#include "pulse_sim.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace pulse {

// Decode a scripted-tape direction index into an integer move vector.
[[nodiscard]] std::pair<std::int8_t, std::int8_t> decode_script_dir(
    std::int32_t d);

// Deterministic scripted input tape. No bot, no search, no model.
[[nodiscard]] FrameInput scripted_input(std::uint32_t tick, std::size_t player,
                                        Character character);

// Runs a single smoke game to completion (or the smoke tick cap), recording
// a replay file to output_dir / "game_NNN.pulse" and filling out GameEvidence.
// Returns false and sets error on failure (e.g. replay save failure).
[[nodiscard]] bool run_smoke_game(const Policy& policy, const GameSchedule& gs,
                                  std::int32_t game_index,
                                  const std::filesystem::path& output_dir,
                                  GameEvidence& out, std::string& error);

}  // namespace pulse
