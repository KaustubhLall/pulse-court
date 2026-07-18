#pragma once

#include "pulse_sim.hpp"
#include "challenge_session.hpp"

#include <cstdint>
#include <string>

namespace pulse {

class ScriptedPolicyController {
public:
    ScriptedPolicyController(const PolicyMetadata& policy, std::int32_t controlled_seat,
                             Character controlled_character, Character opponent_character);

    void reset(const GameState& state);
    FrameInput decide(const GameState& state, std::uint32_t tick, std::int32_t seat);

private:
    PolicyMetadata policy_;
    std::int32_t controlled_seat_;
    Character controlled_character_;
    Character opponent_character_;
    std::uint32_t last_decision_tick_ = 0;
    bool button_held_ = false;
};

struct PolicyLoadResult {
    bool ok;
    PolicyMetadata policy;
    std::string error;
};

class PolicyLoader {
public:
    static PolicyLoadResult load(const std::string& bundle_path,
                                 const std::string& manifest_path);
};

}  // namespace pulse
