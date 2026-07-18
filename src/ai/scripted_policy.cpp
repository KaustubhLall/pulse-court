#include "scripted_policy.hpp"

namespace pulse {

ScriptedPolicyController::ScriptedPolicyController(const PolicyMetadata& policy, std::int32_t controlled_seat,
                                   Character controlled_character, Character opponent_character)
    : policy_(policy), controlled_seat_(controlled_seat),
      controlled_character_(controlled_character), opponent_character_(opponent_character) {
}

void ScriptedPolicyController::reset(const GameState& state) {
    reset_tick_ = state.tick;
    last_decision_tick_ = 0;
    button_held_ = false;
}

FrameInput ScriptedPolicyController::decide(const GameState& state, std::uint32_t tick, std::int32_t seat) {
    (void)seat;

    FrameInput in;
    in.move_x = 0;
    in.move_y = 0;
    in.aim_x = 0;
    in.aim_y = 0;
    in.buttons = 0;

    // Simple deterministic behavior: strike on a 120-tick cadence anchored
    // at the last reset, so a fresh game always opens with a strike.
    const std::uint32_t rel = tick - reset_tick_;
    if (rel % 120 == 0) {
        in.buttons = ButtonStrike;
        button_held_ = true;
    } else {
        button_held_ = false;
        // Move toward center
        in.move_x = (state.core.position.x > 0) ? 1 : -1;
        in.aim_x = in.move_x;
    }

    last_decision_tick_ = tick;
    return in;
}

PolicyLoadResult PolicyLoader::load(const std::string& bundle_path,
                                    const std::string& manifest_path) {
    (void)bundle_path;
    (void)manifest_path;

    PolicyLoadResult result;
    result.ok = false;
    result.error = "Policy loading not yet implemented";
    return result;
}

}  // namespace pulse
