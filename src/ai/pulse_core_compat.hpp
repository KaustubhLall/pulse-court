#pragma once

// ---------------------------------------------------------------------------
// NON-PARITY DEGRADATION SHIM (main-only)
//
// main's src/core is ruleset 2. It does NOT have CoreTouchKind, GameState::
// last_touch_kind, StepResult::goal_last_touch, or StepResult::
// goal_last_touch_kind, all of which exist on ruleset 4 (the pulse-zero-work
// worktree this file was ported from). src/core is READ-ONLY here, so those
// fields cannot be added.
//
// This header provides a degraded compatibility layer so ported pulse_ai
// code that reasons about "how was the core last touched" still compiles
// and produces a deterministic (but LOSSY) answer on main:
//   - main only knows GameState::last_touch (the seat index that last
//     touched the core, or -1 for none). It has NO notion of *how* the
//     touch happened (strike vs. player-body collision vs. anchor well vs.
//     pulse gate).
//   - touch_kind_of()/goal_touch_of() below therefore collapse every real
//     touch into CoreTouchKind::PlayerBody, and "no touch" into
//     CoreTouchKind::None. CoreTouchKind::Strike, ::AnchorWell, and
//     ::PulseGate are NEVER produced by this shim on main.
//
// Any ported logic that specifically requires CoreTouchKind::Strike (e.g.
// the Tactical bot's "certified strike" detector, which checks
// goal_last_touch_kind == CoreTouchKind::Strike) will therefore never see
// that condition satisfied on main and will silently fail closed (return
// "not certified") rather than silently produce a wrong-but-plausible
// answer. This is intentional but IS a behavioral divergence from
// ruleset-4 builds -- do not assume Tactical-bot certification results are
// meaningful on main.
// ---------------------------------------------------------------------------

#include "pulse_sim.hpp"

#include <cstdint>

namespace pulse {

enum class CoreTouchKind : std::uint8_t {
    None = 0,
    Strike = 1,
    PlayerBody = 2,
    AnchorWell = 3,
    PulseGate = 4,
};

// Degraded replacement for ruleset-4 GameState::last_touch_kind.
[[nodiscard]] inline CoreTouchKind touch_kind_of(const GameState& state) noexcept {
    return (state.last_touch < 0) ? CoreTouchKind::None : CoreTouchKind::PlayerBody;
}

// Degraded replacement for ruleset-4 StepResult::goal_last_touch /
// goal_last_touch_kind. main's StepResult carries no touch information at
// all, so callers must pass the GameState observed at (or immediately
// after) the goal-scoring step; this shim reads GameState::last_touch from
// that state instead of a real goal_last_touch value.
[[nodiscard]] inline std::int8_t goal_touch_seat_of(const GameState& state_at_goal) noexcept {
    return state_at_goal.last_touch;
}

[[nodiscard]] inline CoreTouchKind goal_touch_of(const GameState& state_at_goal) noexcept {
    return touch_kind_of(state_at_goal);
}

}  // namespace pulse
