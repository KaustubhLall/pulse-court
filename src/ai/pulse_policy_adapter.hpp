#pragma once

// Shared observation/action adapter for native policy deployment.
//
// This header bridges the gap between the native C++ simulation state and the
// factored policy network input/output format. It reuses the existing
// ``encode_observation`` and ``make_action_mask`` from ``pulse_ai.hpp`` for
// state encoding, and adds:
//
// - ``factors_to_frame_input``: convert canonical factor indices (move, aim,
//   button) into a physical FrameInput for a given player seat, using the
//   same decode_action + mirror path as the vector env.
// - ``mask_button_logits``: apply a button legality mask to button logits
//   (setting illegal positions to -inf, matching the Python
//   ``mask_button_logits`` helper).
// - ``greedy_factors_from_logits``: argmax over move/aim logits and masked
//   button logits, returning canonical factor indices.
// - ``PolicyController``: 12-tick cadence controller wrapping a native
//   ``pulse::Policy``, matching the training cadence exactly (one decision
//   every 12 simulation ticks; movement/aim held; strike/ability/dash
//   edge-triggered on the first tick only).
//
// These functions are pure and allocation-free. They are shared by the
// headless challenge smoke, the viewer challenge mode, and the native policy
// verification tool.

#include "pulse_ai.hpp"
#include "pulse_sim.hpp"

#include <array>
#include <cstdint>
#include <cstddef>

namespace pulse::ai {

// Convert canonical factor indices (move 0-8, aim 0-8, button 0-3) into a
// physical FrameInput for the given player seat. Seat 0 returns the canonical
// input directly; seat 1 mirrors it. Out-of-range factors clamp to 0.
[[nodiscard]] FrameInput factors_to_frame_input(std::int8_t move_idx,
                                                std::int8_t aim_idx,
                                                std::int8_t button_idx,
                                                std::size_t player_index) noexcept;

// Apply a button legality mask to button logits. Illegal positions are set
// to -inf. If no position is legal, the row is returned unchanged (so argmax
// always yields a finite index). ``logits`` and ``mask`` are length-4 arrays;
// the result is written to ``out``.
void mask_button_logits(const std::array<float, 4>& logits,
                        const std::array<std::uint8_t, 4>& mask,
                        std::array<float, 4>& out) noexcept;

// Greedy (argmax) factor selection from raw logits under the authoritative
// button mask. Returns (move, aim, button) canonical factor indices.
// ``move_logits`` and ``aim_logits`` are length-9; ``button_logits`` and
// ``button_mask`` are length-4.
[[nodiscard]] std::array<std::int8_t, 3>
greedy_factors_from_logits(const std::array<float, 9>& move_logits,
                           const std::array<float, 9>& aim_logits,
                           const std::array<float, 4>& button_logits,
                           const std::array<std::uint8_t, 4>& button_mask) noexcept;

}  // namespace pulse::ai

// Forward-declare Policy to avoid a circular include (pulse_policy.hpp
// includes this header).
namespace pulse { class Policy; }

namespace pulse::ai {

// 12-tick PolicyController wrapping a native pulse::Policy.
//
// Matches the training cadence exactly: one decision every
// ``kDecisionIntervalTicks`` (12) simulation ticks. Movement and aim are
// held for the repeat window; strike/ability/dash are edge-triggered on only
// the first tick of each decision. The same trained policy works from either
// seat through the shared perspective transform (encode_observation mirrors
// for seat 1, factors_to_frame_input mirrors the output back).
//
// Decides immediately on first use, reset/tick rewind, phase transition, or
// player-index change; otherwise on state ticks divisible by
// kDecisionIntervalTicks and only once per tick. Returns zero input in
// MatchOver. Idempotent on an unchanged state tick for the same player index.
class PolicyController {
public:
    explicit PolicyController(const Policy& policy) noexcept;

    void reset() noexcept;

    // Returns the held/pulsed input plus a flag marking a real fresh 10 Hz
    // decision (stable for an idempotent same-tick repeat).
    [[nodiscard]] ControllerOutput act(const GameState& state,
                                       std::size_t player_index) noexcept;

    // Whether the last act() call made a fresh decision this tick.
    [[nodiscard]] bool last_decision() const noexcept { return last_decision_; }

private:
    const Policy& policy_;
    bool initialized_ = false;
    std::uint32_t last_seen_tick_ = 0;
    Phase last_phase_ = Phase::MatchOver;
    std::size_t last_player_index_ = 0;
    std::uint32_t last_decision_tick_ = 0;
    FrameInput held_input_{};
    bool last_decision_ = false;
    // Cached output for the current (tick, phase, player) key, for same-key
    // idempotency.
    bool has_cached_ = false;
    std::uint32_t cached_tick_ = 0;
    Phase cached_phase_ = Phase::MatchOver;
    std::size_t cached_player_index_ = 0;
    FrameInput cached_input_{};
    bool cached_decision_ = false;
};

}  // namespace pulse::ai
