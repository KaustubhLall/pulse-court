#pragma once

#include "pulse_sim.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pulse {

inline constexpr std::uint32_t kReplayFormatVersion = 1;

struct RecordedTick {
    std::array<FrameInput, 2> inputs{};
    std::uint64_t expected_hash = 0;
};

struct Replay {
    std::uint32_t format_version = kReplayFormatVersion;
    std::uint32_t ruleset_version = kRulesetVersion;
    std::array<Character, 2> characters{Character::Kite, Character::Vale};
    MatchConfig config{};
    std::vector<RecordedTick> ticks{};
};

struct ReplayVerification {
    bool ok = false;
    std::uint32_t failing_tick = 0;
    std::uint64_t expected_hash = 0;
    std::uint64_t actual_hash = 0;
    std::string message{};
};

[[nodiscard]] bool save_replay(const Replay& replay, const std::string& path, std::string& error);
[[nodiscard]] bool load_replay(const std::string& path, Replay& replay, std::string& error);
[[nodiscard]] ReplayVerification verify_replay(const Replay& replay, MatchConfig config = {});

}  // namespace pulse
