#include "pulse_policy_adapter.hpp"
#include "pulse_policy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pulse::ai {

FrameInput factors_to_frame_input(std::int8_t move_idx,
                                  std::int8_t aim_idx,
                                  std::int8_t button_idx,
                                  std::size_t player_index) noexcept {
    // Clamp to valid ranges.
    auto clamp9 = [](std::int8_t v) -> std::int32_t {
        return (v < 0 || v > 8) ? 0 : static_cast<std::int32_t>(v);
    };
    auto clamp4 = [](std::int8_t v) -> std::int32_t {
        return (v < 0 || v > 3) ? 0 : static_cast<std::int32_t>(v);
    };
    std::int32_t action = clamp9(move_idx) + 9 * clamp9(aim_idx) +
                          81 * clamp4(button_idx);
    FrameInput canonical = Simulation::decode_action(action);
    if (player_index == 0) return canonical;
    return Simulation::mirror_input(canonical);
}

void mask_button_logits(const std::array<float, 4>& logits,
                        const std::array<std::uint8_t, 4>& mask,
                        std::array<float, 4>& out) noexcept {
    // Fail-closed: no-op button (index 0) must always be legal.
    if (mask[0] == 0) {
        // No-op is not legal - this is a fatal error condition.
        // Return all -inf to signal failure.
        const float neg_inf = -std::numeric_limits<float>::infinity();
        for (std::size_t i = 0; i < 4; ++i) {
            out[i] = neg_inf;
        }
        return;
    }

    // Check if any button is legal (should be true since no-op is legal).
    bool any_legal = false;
    for (std::size_t i = 0; i < 4; ++i) {
        if (mask[i] != 0) { any_legal = true; break; }
    }

    const float neg_inf = -std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < 4; ++i) {
        if (any_legal && mask[i] == 0) {
            out[i] = neg_inf;
        } else {
            out[i] = logits[i];
        }
    }
}

std::array<std::int8_t, 3>
greedy_factors_from_logits(const std::array<float, 9>& move_logits,
                           const std::array<float, 9>& aim_logits,
                           const std::array<float, 4>& button_logits,
                           const std::array<std::uint8_t, 4>& button_mask) noexcept {
    // move argmax (strict > for lowest-index tie-breaking)
    std::int8_t move_idx = 0;
    float best_move = move_logits[0];
    for (std::size_t i = 1; i < 9; ++i) {
        if (move_logits[i] > best_move) {
            best_move = move_logits[i];
            move_idx = static_cast<std::int8_t>(i);
        }
    }
    // aim argmax (strict > for lowest-index tie-breaking)
    std::int8_t aim_idx = 0;
    float best_aim = aim_logits[0];
    for (std::size_t i = 1; i < 9; ++i) {
        if (aim_logits[i] > best_aim) {
            best_aim = aim_logits[i];
            aim_idx = static_cast<std::int8_t>(i);
        }
    }
    // masked button argmax (strict > for lowest-index tie-breaking)
    std::array<float, 4> masked{};
    mask_button_logits(button_logits, button_mask, masked);
    std::int8_t button_idx = 0;
    float best_button = masked[0];
    for (std::size_t i = 1; i < 4; ++i) {
        if (masked[i] > best_button) {
            best_button = masked[i];
            button_idx = static_cast<std::int8_t>(i);
        }
    }
    // Check if all masked values are -inf (no legal action)
    const float neg_inf = -std::numeric_limits<float>::infinity();
    if (best_button == neg_inf) {
        // No legal button action - return explicit failure (all zeros)
        return {0, 0, 0};
    }
    return {move_idx, aim_idx, button_idx};
}

// ---- PolicyController -------------------------------------------------------

PolicyController::PolicyController(const Policy& policy) noexcept
    : policy_(policy) {}

void PolicyController::reset() noexcept {
    initialized_ = false;
    last_seen_tick_ = 0;
    last_phase_ = Phase::MatchOver;
    last_player_index_ = 0;
    last_decision_tick_ = 0;
    held_input_ = {};
    last_decision_ = false;
    has_cached_ = false;
    cached_tick_ = 0;
    cached_phase_ = Phase::MatchOver;
    cached_player_index_ = 0;
    cached_input_ = {};
    cached_decision_ = false;
}

ControllerOutput PolicyController::act(const GameState& state,
                                       std::size_t player_index) noexcept {
    if (player_index >= 2) return {FrameInput{}, false};

    const std::uint32_t tick = state.tick;
    const Phase phase = state.phase;

    // Idempotent same-(tick, phase, player) repeat.
    if (has_cached_ && cached_tick_ == tick && cached_phase_ == phase &&
        cached_player_index_ == player_index) {
        return {cached_input_, cached_decision_};
    }

    ControllerOutput out;

    if (phase == Phase::MatchOver) {
        out.input = FrameInput{};
        out.decision = false;
    } else {
        bool first_use = !initialized_;
        bool rewind = initialized_ && (tick < last_seen_tick_);
        bool phase_change = initialized_ && (phase != last_phase_);
        bool player_change = initialized_ && (player_index != last_player_index_);
        bool cadence = is_decision_tick(tick) && (tick != last_decision_tick_);

        bool should_decide = first_use || rewind || phase_change ||
                             player_change || cadence;

        if (should_decide) {
            // Use the native policy's greedy_frame_input, which internally
            // calls encode_observation → forward → greedy_factors →
            // factors_to_frame_input. This is the shared adapter path.
            FrameInput chosen = policy_.greedy_frame_input(state, player_index);
            held_input_ = chosen;
            last_decision_tick_ = tick;
            out.input = chosen;
            out.decision = true;
        } else {
            // Hold movement and aim; pulse button only on the choice tick.
            FrameInput held = held_input_;
            held.buttons = ButtonNone;
            out.input = held;
            out.decision = false;
        }
    }

    initialized_ = true;
    last_seen_tick_ = tick;
    last_phase_ = phase;
    last_player_index_ = player_index;
    last_decision_ = out.decision;
    has_cached_ = true;
    cached_tick_ = tick;
    cached_phase_ = phase;
    cached_player_index_ = player_index;
    cached_input_ = out.input;
    cached_decision_ = out.decision;
    return out;
}

}  // namespace pulse::ai
