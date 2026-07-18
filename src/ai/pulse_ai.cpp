#include "pulse_ai.hpp"

#include "fixed_math.hpp"
#include "pulse_core_compat.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace pulse::ai {

namespace {

// ---- normalization helpers -------------------------------------------------

float clamp11(float v) noexcept {
    if (v < -1.0f) return -1.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

float clamp01(float v) noexcept {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// Center a fixed position component to [-1, 1] across the court span. The
// int64 intermediate prevents overflow of 2*x for malformed large int32
// values before the float conversion.
float center_pos_x(std::int32_t x) noexcept {
    return clamp11(static_cast<float>(static_cast<std::int64_t>(x) * 2) /
                       static_cast<float>(kFieldWidth) - 1.0f);
}
float center_pos_y(std::int32_t y) noexcept {
    return clamp11(static_cast<float>(static_cast<std::int64_t>(y) * 2) /
                       static_cast<float>(kFieldHeight) - 1.0f);
}

// Character-specific cooldown/effect durations for normalization.
std::int32_t ability_cooldown_max(Character c) noexcept {
    switch (c) {
        case Character::Kite: return kJetstepCooldown;
        case Character::Vale: return kAnchorCooldown;
        case Character::Bastion: return kGateCooldown;
    }
    return 1;
}
std::int32_t effect_duration_max(Character c) noexcept {
    switch (c) {
        case Character::Kite: return kJetstepDuration;
        case Character::Vale: return kAnchorDuration;
        case Character::Bastion: return kGateDuration;
    }
    return 1;
}

// Encode one player's 22 observation slots starting at `base`.
void encode_player(const PlayerState& p, Observation& obs,
                   std::size_t base) noexcept {
    obs[base + 0] = center_pos_x(p.position.x);
    obs[base + 1] = center_pos_y(p.position.y);
    obs[base + 2] = clamp11(static_cast<float>(p.velocity.x) /
                            static_cast<float>(kPlayerMaxSpeed));
    obs[base + 3] = clamp11(static_cast<float>(p.velocity.y) /
                            static_cast<float>(kPlayerMaxSpeed));
    obs[base + 4] = clamp11(static_cast<float>(p.facing.x) /
                            static_cast<float>(kFixedScale));
    obs[base + 5] = clamp11(static_cast<float>(p.facing.y) /
                            static_cast<float>(kFixedScale));
    obs[base + 6] = clamp01(static_cast<float>(p.strike_cooldown) /
                            static_cast<float>(kStrikeCooldown));
    obs[base + 7] = clamp01(static_cast<float>(p.dash_cooldown) /
                            static_cast<float>(kDashCooldown));
    obs[base + 8] = clamp01(static_cast<float>(p.ability_cooldown) /
                            static_cast<float>(
                                ability_cooldown_max(p.character)));
    obs[base + 9] = clamp01(static_cast<float>(p.strike_ticks) /
                            static_cast<float>(kStrikeDuration));
    obs[base + 10] = clamp01(static_cast<float>(p.effect_ticks) /
                             static_cast<float>(
                                 effect_duration_max(p.character)));
    if (p.effect_kind == EffectKind::None) {
        obs[base + 11] = 0.0f;
        obs[base + 12] = 0.0f;
    } else {
        obs[base + 11] = center_pos_x(p.effect_position.x);
        obs[base + 12] = center_pos_y(p.effect_position.y);
    }
    // Character one-hot: Kite, Vale, Bastion.
    obs[base + 13] = (p.character == Character::Kite) ? 1.0f : 0.0f;
    obs[base + 14] = (p.character == Character::Vale) ? 1.0f : 0.0f;
    obs[base + 15] = (p.character == Character::Bastion) ? 1.0f : 0.0f;
    // Effect one-hot: none, Jetstep, AnchorWell, PulseGate.
    obs[base + 16] = (p.effect_kind == EffectKind::None) ? 1.0f : 0.0f;
    obs[base + 17] = (p.effect_kind == EffectKind::Jetstep) ? 1.0f : 0.0f;
    obs[base + 18] = (p.effect_kind == EffectKind::AnchorWell) ? 1.0f : 0.0f;
    obs[base + 19] = (p.effect_kind == EffectKind::PulseGate) ? 1.0f : 0.0f;
    obs[base + 20] = p.jetstep_bonus_used ? 1.0f : 0.0f;
    obs[base + 21] = p.strike_has_hit ? 1.0f : 0.0f;
}

// Encode the global observation slots for a canonical state where the observed
// player is player 0. Indices 0-14 are the v1 global block; indices 15-19 add
// the v2 touch-kind one-hot (None, Strike, PlayerBody, AnchorWell, PulseGate).
//
// NON-PARITY (main-only): main's core has no per-touch "kind" (strike vs.
// player-body vs. anchor well vs. pulse gate), only GameState::last_touch
// (which seat, or -1). obs[16], obs[18], obs[19] (Strike / AnchorWell /
// PulseGate) are therefore always 0.0f here; see pulse_core_compat.hpp.
void encode_global(const GameState& s, Observation& obs) noexcept {
    obs[0] = center_pos_x(s.core.position.x);
    obs[1] = center_pos_y(s.core.position.y);
    obs[2] = clamp11(static_cast<float>(s.core.velocity.x) /
                     static_cast<float>(kCoreMaxSpeed));
    obs[3] = clamp11(static_cast<float>(s.core.velocity.y) /
                     static_cast<float>(kCoreMaxSpeed));
    // Phase one-hot: kickoff, live, match over.
    obs[4] = (s.phase == Phase::Kickoff) ? 1.0f : 0.0f;
    obs[5] = (s.phase == Phase::Live) ? 1.0f : 0.0f;
    obs[6] = (s.phase == Phase::MatchOver) ? 1.0f : 0.0f;
    obs[7] = (s.config.kickoff_ticks > 0)
                 ? clamp01(static_cast<float>(s.kickoff_remaining) /
                           static_cast<float>(s.config.kickoff_ticks))
                 : 0.0f;
    obs[8] = (s.config.regulation_ticks > 0)
                 ? clamp01(static_cast<float>(s.regulation_remaining) /
                           static_cast<float>(s.config.regulation_ticks))
                 : 0.0f;
    obs[9] = s.golden_goal ? 1.0f : 0.0f;
    obs[10] = (s.config.score_to_win > 0)
                  ? clamp01(static_cast<float>(s.score[0]) /
                            static_cast<float>(s.config.score_to_win))
                  : 0.0f;
    obs[11] = (s.config.score_to_win > 0)
                  ? clamp01(static_cast<float>(s.score[1]) /
                            static_cast<float>(s.config.score_to_win))
                  : 0.0f;
    // Last-touch one-hot: none, self (player 0), opponent (player 1).
    obs[12] = (s.last_touch == -1) ? 1.0f : 0.0f;
    obs[13] = (s.last_touch == 0) ? 1.0f : 0.0f;
    obs[14] = (s.last_touch == 1) ? 1.0f : 0.0f;
    // Touch-kind one-hot (v2, degraded on main -- see pulse_core_compat.hpp):
    // None, Strike, PlayerBody, AnchorWell, PulseGate.
    const CoreTouchKind kind = touch_kind_of(s);
    obs[15] = (kind == CoreTouchKind::None) ? 1.0f : 0.0f;
    obs[16] = (kind == CoreTouchKind::Strike) ? 1.0f : 0.0f;
    obs[17] = (kind == CoreTouchKind::PlayerBody) ? 1.0f : 0.0f;
    obs[18] = (kind == CoreTouchKind::AnchorWell) ? 1.0f : 0.0f;
    obs[19] = (kind == CoreTouchKind::PulseGate) ? 1.0f : 0.0f;
}

// ---- geometry helpers (integer/fixed-point) --------------------------------

std::int8_t sign_dead(std::int64_t v, std::int64_t t) noexcept {
    if (v > t) return 1;
    if (v < -t) return -1;
    return 0;
}

std::int64_t dist_sq(std::int32_t ax, std::int32_t ay, std::int32_t bx,
                     std::int32_t by) noexcept {
    std::int64_t dx = static_cast<std::int64_t>(ax) - bx;
    std::int64_t dy = static_cast<std::int64_t>(ay) - by;
    return dx * dx + dy * dy;
}

std::int32_t distance(std::int32_t ax, std::int32_t ay, std::int32_t bx,
                      std::int32_t by) noexcept {
    return static_cast<std::int32_t>(isqrt64(static_cast<std::uint64_t>(
        dist_sq(ax, ay, bx, by))));
}

std::int32_t clamp_i32(std::int32_t v, std::int32_t lo,
                       std::int32_t hi) noexcept {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Reflect a value into [lo, hi] as a triangle wave (deterministic y-wall
// reflection for predicted core positions).
std::int32_t reflect_into(std::int32_t v, std::int32_t lo,
                          std::int32_t hi) noexcept {
    std::int32_t range = hi - lo;
    if (range <= 0) return lo;
    std::int64_t r = static_cast<std::int64_t>(v) - lo;
    std::int64_t span = static_cast<std::int64_t>(2) * range;
    r %= span;
    if (r < 0) r += span;
    if (r > range) r = span - r;
    return lo + static_cast<std::int32_t>(r);
}

std::int32_t abs_i32(std::int32_t v) noexcept {
    return (v < 0) ? -v : v;
}

// Legality checks matching the factor mask.
bool strike_legal(const PlayerState& p) noexcept {
    return p.strike_cooldown == 0 && p.strike_ticks == 0;
}
bool ability_legal(const PlayerState& p) noexcept {
    return p.ability_cooldown == 0;
}
bool dash_legal(const PlayerState& p) noexcept {
    return p.dash_cooldown == 0;
}

constexpr std::int64_t kDeadzone = kFixedScale / 4;

// ---- scripted bot logic (canonical player 0 attacking right) --------------

FrameInput chaser_canonical(const GameState& s) noexcept {
    const PlayerState& p = s.players[0];
    const CoreState& c = s.core;
    std::int64_t dx = c.position.x - p.position.x;
    std::int64_t dy = c.position.y - p.position.y;
    std::int32_t dist = distance(p.position.x, p.position.y, c.position.x,
                                 c.position.y);

    FrameInput in;
    in.move_x = sign_dead(dx, kDeadzone);
    in.move_y = sign_dead(dy, kDeadzone);
    in.aim_x = in.move_x;
    in.aim_y = in.move_y;

    if (strike_legal(p) && dist <= kStrikeReach + kCoreRadius) {
        in.buttons = ButtonStrike;
    } else if (ability_legal(p) && dist <= 6 * kFixedScale) {
        in.buttons = ButtonAbility;
    } else if (dash_legal(p) && dist > 8 * kFixedScale) {
        in.buttons = ButtonDash;
    }
    return in;
}

FrameInput goalie_canonical(const GameState& s) noexcept {
    const PlayerState& p = s.players[0];
    const CoreState& c = s.core;
    constexpr std::int32_t anchor_x = 4 * kFixedScale;
    constexpr std::int32_t y_lo = kGoalTop + kPlayerRadius;
    constexpr std::int32_t y_hi = kGoalBottom - kPlayerRadius;

    bool own_third = c.position.x < kFieldWidth / 3;
    bool approaching = c.velocity.x < 0;
    bool own_half = c.position.x < kFieldHalfX;
    bool engage = own_third || approaching;

    FrameInput in;

    if (engage) {
        std::int32_t target_x;
        std::int32_t target_y;
        if (approaching && c.position.x > anchor_x) {
            // Predict core y at the anchor x using integer time.
            std::int64_t dx = c.position.x - anchor_x;
            std::int32_t pred_y = c.position.y;
            if (c.velocity.x != 0 && dx > 0) {
                std::int64_t t = dx / (-static_cast<std::int64_t>(c.velocity.x));
                pred_y = c.position.y +
                         static_cast<std::int32_t>(
                             static_cast<std::int64_t>(c.velocity.y) * t);
                pred_y = reflect_into(pred_y, kCoreRadius,
                                      kFieldHeight - kCoreRadius);
            }
            target_x = anchor_x;
            target_y = clamp_i32(pred_y, y_lo, y_hi);
        } else {
            target_x = c.position.x;
            target_y = clamp_i32(c.position.y, y_lo, y_hi);
        }

        std::int64_t mdx = target_x - p.position.x;
        std::int64_t mdy = target_y - p.position.y;
        in.move_x = sign_dead(mdx, kDeadzone);
        in.move_y = sign_dead(mdy, kDeadzone);
        // Aim at the core when engaging.
        in.aim_x = sign_dead(c.position.x - p.position.x, 0);
        in.aim_y = sign_dead(c.position.y - p.position.y, 0);

        std::int32_t dist = distance(p.position.x, p.position.y, c.position.x,
                                     c.position.y);
        bool core_behind = c.position.x < p.position.x - kCoreRadius;

        // Button selection: strike > ability > dash. Dash is considered
        // whenever no Strike/Ability was actually chosen, so an off-cooldown
        // but contextually unused ability does not suppress Dash.
        std::uint8_t chosen = ButtonNone;
        if (!core_behind && strike_legal(p) &&
            dist <= kPlayerRadius + kCoreRadius + kStrikeReach) {
            chosen = ButtonStrike;
        } else if (ability_legal(p)) {
            if (p.character == Character::Kite && dist <= 6 * kFixedScale) {
                chosen = ButtonAbility;
            } else if ((p.character == Character::Bastion ||
                        p.character == Character::Vale) &&
                       own_half && approaching) {
                chosen = ButtonAbility;
            }
        }
        if (chosen == ButtonNone && dash_legal(p) && dist > 6 * kFixedScale) {
            chosen = ButtonDash;
        }
        in.buttons = chosen;
    } else {
        // Recover to the anchor and track the core y.
        std::int32_t target_y = clamp_i32(c.position.y, y_lo, y_hi);
        std::int64_t mdx = anchor_x - p.position.x;
        std::int64_t mdy = target_y - p.position.y;
        in.move_x = sign_dead(mdx, kDeadzone);
        in.move_y = sign_dead(mdy, kDeadzone);
        // Aim rightward while recovering.
        in.aim_x = 1;
        in.aim_y = 0;
    }
    return in;
}

// ---- Tactical v3: certified one-ply strike selection -----------------------
//
// When Tactical is close enough and legal to strike, evaluate exactly three
// canonical goalward candidate aims in deterministic priority order:
//   1. (1, 0)  — horizontal, preferred on an equal result
//   2. (1, -1)
//   3. (1, 1)
//
// For each candidate, clone the current canonical GameState into a local
// authoritative Simulation (never reimplement physics). On lookahead tick 1,
// submit Tactical's already-computed movement, the candidate aim, and a
// one-tick ButtonStrike pulse for canonical player 0; player 1 is idle. For
// the remaining ticks through the first kDecisionIntervalTicks policy
// interval, hold the candidate movement/aim with ButtonNone. After that
// interval, both players receive zero input. Simulate for at most 4*kSimHz
// ticks, stopping at the first goal or match finish.
//
// A candidate is certified only if ALL hold:
//   - a fresh actor-0 StrikeHit occurred in this candidate rollout at or
//     before the first goal (the candidate's tick-1 strike pulse actually
//     connected; without this, a goal could carry stale touch provenance
//     from a strike that happened before the cloned state);
//   - the first goal is scored by canonical team 0;
//   - the touch attributed to the goal is seat 0 with CoreTouchKind::Strike.
//
// NON-PARITY (main-only): main's core cannot distinguish a strike touch from
// a player-body touch (see pulse_core_compat.hpp), so the CoreTouchKind::
// Strike condition below is NEVER true on main. Tactical's certified-strike
// path therefore always fails closed (no candidate ever certifies) on main;
// Strike is suppressed and the ability/dash fallback chain runs instead.
// This is a real, documented behavioral divergence from ruleset-4 builds,
// not a bug -- do not "fix" it by loosening the certification predicate.
//
// Among certified candidates, choose the one that scores in the fewest
// lookahead ticks; exact tick ties retain candidate priority (horizontal
// wins).

struct CertifiedStrike {
    bool found = false;
    std::int8_t aim_x = 0;
    std::int8_t aim_y = 0;
    std::int32_t goal_tick = 0;
};

// Per-candidate rollout result. `fresh_strike_hit` is true iff an authoritative
// actor-0 StrikeHit occurred at or before the first goal in this candidate's
// rollout. `certified` is the full certification predicate (fresh hit + team-0
// + seat-0 Strike-kind touch, per pulse_core_compat.hpp on main).
struct CandidateResult {
    bool certified = false;
    bool fresh_strike_hit = false;
    std::int32_t goal_tick = 0;
};

// Evaluate one candidate aim. Reproduces the locked Tactical v3 rollout
// contract: tick-1 strike pulse for canonical player 0, held movement/aim for
// the first kDecisionIntervalTicks ticks, idle opponent, zero input after the
// interval, at most 4*kSimHz ticks, stop on first goal/match finish.
CandidateResult evaluate_candidate(const GameState& s, std::int8_t move_x,
                                   std::int8_t move_y, std::int8_t aim_x,
                                   std::int8_t aim_y) noexcept {
    Simulation sim;
    sim.set_state(s);

    CandidateResult res;
    bool fresh_strike_hit = false;
    for (std::int32_t t = 1; t <= 4 * kSimHz; ++t) {
        std::array<FrameInput, 2> in{};
        if (t <= kDecisionIntervalTicks) {
            in[0].move_x = move_x;
            in[0].move_y = move_y;
            in[0].aim_x = aim_x;
            in[0].aim_y = aim_y;
            if (t == 1) in[0].buttons = ButtonStrike;
        }
        // t > kDecisionIntervalTicks: both players zero (already zero).
        StepEvents events;
        StepResult r = sim.step(in, &events);
        for (std::uint8_t i = 0; i < events.count; ++i) {
            const SimulationEvent& e = events.events[i];
            if (e.type == SimulationEventType::StrikeHit && e.actor == 0) {
                fresh_strike_hit = true;
            }
        }
        if (r.goal_scored) {
            res.fresh_strike_hit = fresh_strike_hit;
            // NON-PARITY: goal_touch_seat_of/goal_touch_of degrade from the
            // post-step GameState (main has no goal_last_touch fields).
            const GameState& after = sim.state();
            if (fresh_strike_hit && r.scoring_team == 0 &&
                goal_touch_seat_of(after) == 0 &&
                goal_touch_of(after) == CoreTouchKind::Strike) {
                res.certified = true;
                res.goal_tick = t;
            }
            return res;
        }
        if (r.match_finished) {
            res.fresh_strike_hit = fresh_strike_hit;
            return res;
        }
    }
    res.fresh_strike_hit = fresh_strike_hit;
    return res;
}

CertifiedStrike certify_strike(const GameState& s, std::int8_t move_x,
                               std::int8_t move_y) noexcept {
    struct Cand {
        std::int8_t ax;
        std::int8_t ay;
    };
    constexpr Cand cands[3] = {
        {1, 0},    // horizontal, preferred on tie
        {1, -1},
        {1, 1},
    };

    CertifiedStrike best;
    best.goal_tick = std::numeric_limits<std::int32_t>::max();

    for (const Cand& c : cands) {
        CandidateResult res =
            evaluate_candidate(s, move_x, move_y, c.ax, c.ay);
        if (res.certified) {
            // Strictly smaller tick replaces; ties keep earlier (higher-
            // priority) candidate because we iterate in priority order.
            if (!best.found || res.goal_tick < best.goal_tick) {
                best.found = true;
                best.aim_x = c.ax;
                best.aim_y = c.ay;
                best.goal_tick = res.goal_tick;
            }
        }
    }
    return best;
}

FrameInput tactical_canonical(const GameState& s) noexcept {
    const PlayerState& p = s.players[0];
    const PlayerState& opp = s.players[1];
    const CoreState& c = s.core;

    std::int32_t dist = distance(p.position.x, p.position.y, c.position.x,
                                 c.position.y);
    // Reachable intercept horizon in ticks, clamped to 0..48. Restored to the
    // locked contract value (the v1 drift to 24 is corrected here).
    std::int32_t horizon = (kPlayerMaxSpeed > 0)
                               ? clamp_i32(dist / kPlayerMaxSpeed, 0, 48)
                               : 0;

    // Predict core x/y with integer velocity, reflect y, clamp x.
    std::int32_t pred_x = c.position.x +
                          static_cast<std::int32_t>(
                              static_cast<std::int64_t>(c.velocity.x) * horizon);
    std::int32_t pred_y = c.position.y +
                          static_cast<std::int32_t>(
                              static_cast<std::int64_t>(c.velocity.y) * horizon);
    pred_y = reflect_into(pred_y, kCoreRadius, kFieldHeight - kCoreRadius);
    pred_x = clamp_i32(pred_x, kCoreRadius, kFieldWidth - kCoreRadius);

    bool own_third = c.position.x < kFieldWidth / 3;
    bool own_half = c.position.x < kFieldHalfX;
    // "moving strongly left" in the own half.
    bool strongly_left = c.velocity.x < -(kCoreMaxSpeed / 4);
    bool dangerous = own_third || (own_half && strongly_left);

    std::int32_t target_x;
    std::int32_t target_y;
    if (dangerous) {
        // Defensive intercept at the predicted core position.
        target_x = pred_x;
        target_y = pred_y;
    } else {
        // Lineup point 2*kFixedScale behind the predicted core (toward own
        // side) so the approach aims goalward through the core. Restored to
        // the locked contract value (the v1 drift to 1*kFixedScale is
        // corrected here).
        target_x = clamp_i32(pred_x - 2 * kFixedScale, kCoreRadius,
                             kFieldWidth - kCoreRadius);
        target_y = pred_y;
    }

    FrameInput in;
    // Core is "behind" when it is more than kCoreRadius past the player toward
    // our own goal. Never strike a core more than kCoreRadius behind.
    bool core_behind = c.position.x < p.position.x - kCoreRadius;
    bool core_behind_strike = core_behind;

    // ---- circle-around when core_behind -----------------------------------
    // When the core is between the player and the own goal, moving left
    // directly would body-bump it toward the own goal. Instead, circle
    // around: first create y-separation, then move left past the core only
    // when the y-gap is large enough that the player won't collide with it.
    constexpr std::int32_t safe_y_gap = kPlayerRadius + kCoreRadius +
                                        kFixedScale;

    std::int8_t circle_y = 0;  // y-direction for circling (also used for aim)

    // Determine the y-direction to circle: away from the core, or toward
    // center if near a wall.
    {
        bool near_top = p.position.y < kPlayerRadius + 2 * kFixedScale;
        bool near_bot = p.position.y > kFieldHeight - kPlayerRadius -
                        2 * kFixedScale;
        if (near_top)
            circle_y = 1;
        else if (near_bot)
            circle_y = -1;
        else
            circle_y = (p.position.y >= c.position.y) ? 1 : -1;
    }

    if (core_behind) {
        std::int32_t y_gap = abs_i32(p.position.y - c.position.y);
        if (y_gap >= safe_y_gap) {
            // Safe to move left past the core without colliding.
            in.move_x = -1;
            in.move_y = circle_y;
        } else {
            // Not enough y-separation: move perpendicular to get around the
            // core. Never move left here — that would bump the core.
            in.move_x = 0;
            in.move_y = circle_y;
        }
    } else {
        // Normal approach: move toward the target.
        std::int64_t mdx = target_x - p.position.x;
        std::int64_t mdy = target_y - p.position.y;
        in.move_x = sign_dead(mdx, kDeadzone);
        in.move_y = sign_dead(mdy, kDeadzone);
    }

    // ---- bank-shot aim (Task 3) -------------------------------------------
    // Goal lanes just inside the goal mouth.
    constexpr std::int32_t top_lane = kGoalTop + kFixedScale;
    constexpr std::int32_t bottom_lane = kGoalBottom - kFixedScale;
    constexpr std::int64_t aim_deadzone = kFixedScale / 2;

    // A blocker exists when the opponent is goal-side of the core (between the
    // core and the right goal), no more than 12*kFixedScale farther in x, and
    // within 3*kFixedScale in y.
    bool opp_goal_side = opp.position.x > c.position.x;
    std::int32_t opp_x_gap = opp.position.x - c.position.x;
    std::int32_t opp_y_gap = abs_i32(opp.position.y - c.position.y);
    bool blocked = opp_goal_side && opp_x_gap <= 12 * kFixedScale &&
                   opp_y_gap <= 3 * kFixedScale;

    bool close = dist <= 5 * kFixedScale;

    if (core_behind) {
        // While circling, aim follows the circle-around path so the facing
        // stays consistent with the recovery movement. Dash is deliberately
        // suppressed while core_behind (see button selection below), so this
        // aim never enables a dash into the core; it only keeps the facing
        // aligned for the eventual strike once the core is no longer behind.
        // When y-separation is sufficient and the bot is moving left past
        // the core, aim left to continue the circle-around direction.
        std::int32_t y_gap = abs_i32(p.position.y - c.position.y);
        if (y_gap >= safe_y_gap) {
            in.aim_x = -1;
            in.aim_y = circle_y;
        } else {
            in.aim_x = 0;
            in.aim_y = circle_y;
        }
    } else if (close) {
        // Aim +x (goalward). Choose vertical aim toward a goal lane.
        in.aim_x = 1;
        std::int32_t lane_y;
        if (blocked) {
            // Aim toward whichever goal lane is farther from the opponent's
            // current y; ties choose the top lane. This creates a
            // deterministic diagonal bank shot around a center defender.
            std::int32_t d_top = abs_i32(opp.position.y - top_lane);
            std::int32_t d_bot = abs_i32(opp.position.y - bottom_lane);
            lane_y = (d_top >= d_bot) ? top_lane : bottom_lane;
        } else {
            // Not blocked: aim toward goal center.
            lane_y = kFieldHalfY;
        }
        in.aim_y = sign_dead(static_cast<std::int64_t>(lane_y) -
                                 p.position.y, aim_deadzone);
    } else {
        // Far: aim toward the predicted lineup to guide the approach.
        in.aim_x = sign_dead(static_cast<std::int64_t>(target_x) -
                                 p.position.x, 0);
        in.aim_y = sign_dead(static_cast<std::int64_t>(target_y) -
                                 p.position.y, 0);
    }

    std::int32_t target_dist = distance(p.position.x, p.position.y, target_x,
                                        target_y);

    // Button selection: strike > ability > dash.
    //
    // Tactical v3 (A): when close enough and legal to strike, do not accept
    // the quantized aim immediately. Run a certified one-ply rollout over
    // three canonical goalward candidate aims. Only press Strike if a
    // candidate certifies (a fresh actor-0 StrikeHit occurred in the rollout
    // at or before the first goal, the first goal is team 0, and the goal
    // touch is attributed to seat 0 with CoreTouchKind::Strike -- always
    // false on main, see the NON-PARITY note above certify_strike). The
    // certified aim overrides the computed aim; the movement stays. If no
    // candidate certifies, Strike is suppressed and the existing
    // ability/dash chain runs.
    std::uint8_t chosen = ButtonNone;
    if (strike_legal(p) && !core_behind_strike &&
        dist <= kPlayerRadius + kCoreRadius + kStrikeReach) {
        CertifiedStrike cert = certify_strike(s, in.move_x, in.move_y);
        if (cert.found) {
            chosen = ButtonStrike;
            in.aim_x = cert.aim_x;
            in.aim_y = cert.aim_y;
        }
    }
    if (chosen == ButtonNone && ability_legal(p)) {
        if (p.character == Character::Kite && !core_behind &&
            dist <= 7 * kFixedScale &&
            p.effect_kind != EffectKind::Jetstep) {
            chosen = ButtonAbility;
        } else if (p.character == Character::Vale) {
            // Behind/near the core so an Anchor Well ahead pulls goalward, or
            // defensively on an approaching core. Never when core_behind:
            // the well is placed ahead in the facing direction, which may
            // not be goalward while circling, so it could pull the core
            // toward the own goal.
            bool behind_near = p.position.x <= c.position.x + kPlayerRadius;
            bool defensive = (c.velocity.x < 0 && own_half);
            if (!core_behind && (behind_near || defensive))
                chosen = ButtonAbility;
        } else if (p.character == Character::Bastion) {
            // Defensively for an approaching core, or offensively when a gate
            // ahead will keep the core in the opponent half. Never when
            // core_behind: the gate is placed ahead in the facing direction,
            // which may not be goalward while circling, so it could bounce
            // the core toward the own goal.
            bool defensive = (c.velocity.x < 0 && own_half);
            bool offensive = (c.position.x > kFieldHalfX &&
                              p.position.x < c.position.x);
            if (!core_behind && (defensive || offensive))
                chosen = ButtonAbility;
        }
    }
    // Tactical v3 (B): safe core-behind Dash recovery. When core_behind,
    // allow Dash only when no Strike/Ability was chosen, Dash is legal, and
    // center-to-center player/core distance is strictly greater than
    // 4*kFixedScale. The Dash follows the circle-around aim already computed
    // (vertical while y-separation is insufficient, diagonal left-and-away
    // once safe_y_gap is reached). At distance <= 4*kFixedScale, Dash stays
    // suppressed to avoid propelling the player into the core. When the core
    // is not behind, the existing target_dist > 6*kFixedScale rule is
    // preserved exactly.
    if (chosen == ButtonNone && dash_legal(p)) {
        if (core_behind) {
            if (dist > 4 * kFixedScale) {
                chosen = ButtonDash;
            }
        } else if (target_dist > 6 * kFixedScale) {
            chosen = ButtonDash;
        }
    }
    in.buttons = chosen;
    return in;
}

FrameInput choose_canonical(BotKind kind, const GameState& s) noexcept {
    if (s.phase == Phase::MatchOver) return FrameInput{};
    switch (kind) {
        case BotKind::Idle: return FrameInput{};
        case BotKind::Chaser: return chaser_canonical(s);
        case BotKind::Goalie: return goalie_canonical(s);
        case BotKind::Tactical: return tactical_canonical(s);
    }
    return FrameInput{};
}

// ---- SplitMix64 (private, local) -------------------------------------------

struct SplitMix64 {
    std::uint64_t s;
    explicit SplitMix64(std::uint64_t seed) noexcept : s(seed) {}
    std::uint64_t next() noexcept {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    std::int32_t next_i32(std::int32_t lo, std::int32_t hi) noexcept {
        if (hi <= lo) return lo;
        std::uint64_t r = next();
        return lo + static_cast<std::int32_t>(r % static_cast<std::uint64_t>(hi - lo));
    }
};

}  // namespace

// ---- internal test seam (not declared in header) ---------------------------
//
// Exposes the Tactical v3 certified-strike helpers for focused unit tests
// without adding them to the public API. Uses output parameters to avoid
// cross-TU struct type mismatch in MSVC name mangling. Defined inside the
// existing pulse::ai namespace, so the fully-qualified names are
// pulse::ai::detail::test_certify_strike and
// pulse::ai::detail::test_certify_strike_candidates.

namespace detail {

void test_certify_strike(const GameState& s, std::int8_t move_x,
                         std::int8_t move_y, bool& found,
                         std::int8_t& aim_x, std::int8_t& aim_y,
                         std::int32_t& goal_tick) noexcept {
    CertifiedStrike cs = certify_strike(s, move_x, move_y);
    found = cs.found;
    aim_x = cs.aim_x;
    aim_y = cs.aim_y;
    goal_tick = cs.goal_tick;
}

// Per-candidate certification in locked priority order:
//   [0] = (1,0), [1] = (1,-1), [2] = (1,1).
// Exposes candidate-level results so focused tests can prove shortest-tick
// selection and exact-tick priority ties from the same rollout the
// production helper uses, without a public API.
void test_certify_strike_candidates(const GameState& s, std::int8_t move_x,
                                    std::int8_t move_y, bool certified[3],
                                    bool fresh_hit[3],
                                    std::int32_t goal_tick[3]) noexcept {
    struct Cand {
        std::int8_t ax;
        std::int8_t ay;
    };
    constexpr Cand cands[3] = {{1, 0}, {1, -1}, {1, 1}};
    for (int i = 0; i < 3; ++i) {
        CandidateResult res =
            evaluate_candidate(s, move_x, move_y, cands[i].ax, cands[i].ay);
        certified[i] = res.certified;
        fresh_hit[i] = res.fresh_strike_hit;
        goal_tick[i] = res.certified ? res.goal_tick : 0;
    }
}

}  // namespace detail

// ---- public API ------------------------------------------------------------

const char* bot_kind_name(BotKind kind) noexcept {
    switch (kind) {
        case BotKind::Idle: return "idle";
        case BotKind::Chaser: return "chaser";
        case BotKind::Goalie: return "goalie";
        case BotKind::Tactical: return "tactical";
    }
    return "unknown";
}

namespace {
// Safe ASCII lowering for unsigned-char / custom case-insensitive comparison.
char ascii_lower(char ch) noexcept {
    auto u = static_cast<unsigned char>(ch);
    if (u >= 'A' && u <= 'Z') return static_cast<char>(u - 'A' + 'a');
    return ch;
}
bool ieq_ascii(const char* a, const char* b) noexcept {
    while (*a && *b) {
        if (ascii_lower(*a) != ascii_lower(*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}
}  // namespace

bool parse_bot_kind(const char* text, BotKind& kind) noexcept {
    if (!text) return false;
    if (ieq_ascii(text, "idle")) { kind = BotKind::Idle; return true; }
    if (ieq_ascii(text, "chaser")) { kind = BotKind::Chaser; return true; }
    if (ieq_ascii(text, "goalie")) { kind = BotKind::Goalie; return true; }
    if (ieq_ascii(text, "tactical")) { kind = BotKind::Tactical; return true; }
    return false;
}

Observation encode_observation(const GameState& state,
                               std::size_t player_index) noexcept {
    Observation obs{};
    if (player_index >= 2) return obs;

    GameState canonical;
    if (player_index == 0) {
        canonical = state;
    } else {
        canonical = Simulation::mirror_state(state);
    }
    encode_global(canonical, obs);
    // v2: self player block at 20-41, opponent at 42-63.
    encode_player(canonical.players[0], obs, 20);
    encode_player(canonical.players[1], obs, 42);
    return obs;
}

ActionMask make_action_mask(const GameState& state,
                            std::size_t player_index) noexcept {
    ActionMask m{};
    if (player_index >= 2) return m;

    const PlayerState* pp = (player_index == 0) ? &state.players[0]
                                                : &state.players[1];
    // Movement and aim are always fully legal.
    for (std::size_t i = 0; i < 9; ++i) {
        m.move[i] = 1;
        m.aim[i] = 1;
    }
    m.button[0] = 1;  // none always legal
    m.button[1] = strike_legal(*pp) ? 1 : 0;
    m.button[2] = ability_legal(*pp) ? 1 : 0;
    m.button[3] = dash_legal(*pp) ? 1 : 0;
    return m;
}

FrameInput choose_scripted_action(BotKind kind, const GameState& state,
                                  std::size_t player_index) noexcept {
    if (player_index >= 2) return FrameInput{};
    if (player_index == 0) return choose_canonical(kind, state);
    GameState mirrored = Simulation::mirror_state(state);
    FrameInput canonical_in = choose_canonical(kind, mirrored);
    return Simulation::mirror_input(canonical_in);
}

void BotController::reset() noexcept {
    initialized_ = false;
    last_seen_tick_ = 0;
    last_phase_ = Phase::MatchOver;
    last_player_index_ = 0;
    last_decision_tick_ = 0;
    held_input_ = {};
    has_cached_ = false;
    cached_tick_ = 0;
    cached_phase_ = Phase::MatchOver;
    cached_player_index_ = 0;
    cached_input_ = {};
    cached_decision_ = false;
}

ControllerOutput BotController::act(const GameState& state,
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
            FrameInput chosen = choose_scripted_action(kind_, state, player_index);
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
    has_cached_ = true;
    cached_tick_ = tick;
    cached_phase_ = phase;
    cached_player_index_ = player_index;
    cached_input_ = out.input;
    cached_decision_ = out.decision;
    return out;
}

// ---- scenario generator ----------------------------------------------------

namespace {

constexpr std::int32_t kScenarioMaxRetries = 1000;
constexpr std::int32_t kScenarioMaxRetriesV2 = 4096;

// Safe fallback layout for v1: deterministic, always valid under v1 rules.
GameState scenario_fallback(std::array<Character, 2> characters,
                            std::int32_t max_ticks) noexcept {
    GameState s{};
    s.ruleset_version = kRulesetVersion;
    s.tick = 0;
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    s.regulation_remaining = max_ticks;
    s.golden_goal = false;
    s.score = {0, 0};
    s.last_touch = -1;
    s.config = {1, max_ticks, 0};
    s.players[0].character = characters[0];
    s.players[1].character = characters[1];
    s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
    s.players[1].position = {3 * kFieldWidth / 4, kFieldHalfY};
    s.players[0].facing = {kFixedScale, 0};
    s.players[1].facing = {-kFixedScale, 0};
    s.core.position = {kFieldHalfX, kFieldHalfY};
    s.core.velocity = {kCoreMaxSpeed / 2, 0};
    return s;
}

// Safe fallback layout for v2: quarter-field players, stationary center core,
// satisfying all v2 invariants.
GameState scenario_fallback_v2(std::array<Character, 2> characters,
                               std::int32_t max_ticks) noexcept {
    GameState s{};
    s.ruleset_version = kRulesetVersion;
    s.tick = 0;
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    s.regulation_remaining = max_ticks;
    s.golden_goal = false;
    s.score = {0, 0};
    s.last_touch = -1;
    s.config = {1, max_ticks, 0};
    s.players[0].character = characters[0];
    s.players[1].character = characters[1];
    s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
    s.players[1].position = {3 * kFieldWidth / 4, kFieldHalfY};
    s.players[0].facing = {kFixedScale, 0};
    s.players[1].facing = {-kFixedScale, 0};
    s.core.position = {kFieldHalfX, kFieldHalfY};
    s.core.velocity = {0, 0};
    return s;
}

bool circles_in_bounds(const GameState& s) noexcept {
    if (s.players[0].position.x < kPlayerRadius ||
        s.players[0].position.x > kFieldWidth - kPlayerRadius ||
        s.players[0].position.y < kPlayerRadius ||
        s.players[0].position.y > kFieldHeight - kPlayerRadius)
        return false;
    if (s.players[1].position.x < kPlayerRadius ||
        s.players[1].position.x > kFieldWidth - kPlayerRadius ||
        s.players[1].position.y < kPlayerRadius ||
        s.players[1].position.y > kFieldHeight - kPlayerRadius)
        return false;
    if (s.core.position.x < kCoreRadius ||
        s.core.position.x > kFieldWidth - kCoreRadius ||
        s.core.position.y < kCoreRadius ||
        s.core.position.y > kFieldHeight - kCoreRadius)
        return false;
    return true;
}

bool no_overlap(const GameState& s) noexcept {
    std::int64_t pp = dist_sq(s.players[0].position.x, s.players[0].position.y,
                              s.players[1].position.x, s.players[1].position.y);
    if (pp <= static_cast<std::int64_t>(2 * kPlayerRadius) *
                  static_cast<std::int64_t>(2 * kPlayerRadius))
        return false;
    std::int64_t pc0 = dist_sq(s.players[0].position.x,
                               s.players[0].position.y, s.core.position.x,
                               s.core.position.y);
    if (pc0 <= static_cast<std::int64_t>(kPlayerRadius + kCoreRadius) *
                   static_cast<std::int64_t>(kPlayerRadius + kCoreRadius))
        return false;
    std::int64_t pc1 = dist_sq(s.players[1].position.x,
                               s.players[1].position.y, s.core.position.x,
                               s.core.position.y);
    if (pc1 <= static_cast<std::int64_t>(kPlayerRadius + kCoreRadius) *
                   static_cast<std::int64_t>(kPlayerRadius + kCoreRadius))
        return false;
    return true;
}

// Each player/core center distance at least 3*kFixedScale (v2 invariant).
bool min_player_core_sep(const GameState& s) noexcept {
    std::int64_t min_sep = static_cast<std::int64_t>(3 * kFixedScale);
    std::int64_t min_sep2 = min_sep * min_sep;
    std::int64_t pc0 = dist_sq(s.players[0].position.x,
                               s.players[0].position.y, s.core.position.x,
                               s.core.position.y);
    if (pc0 < min_sep2) return false;
    std::int64_t pc1 = dist_sq(s.players[1].position.x,
                               s.players[1].position.y, s.core.position.x,
                               s.core.position.y);
    if (pc1 < min_sep2) return false;
    return true;
}

// Idle-safety probe: copy the state, set the probe's regulation config/remaining
// to at least the safety horizon so a short match clock cannot terminate early,
// then step authoritative Simulation with zero inputs for
// kScenarioIdleSafetyTicks. Returns true if no goal is scored.
bool idle_safety_probe(const GameState& s) noexcept {
    GameState probe = s;
    probe.config.regulation_ticks = kScenarioIdleSafetyTicks;
    probe.regulation_remaining = kScenarioIdleSafetyTicks;
    Simulation sim;
    sim.set_state(probe);
    std::array<FrameInput, 2> zero{};
    for (std::int32_t t = 0; t < kScenarioIdleSafetyTicks; ++t) {
        StepResult r = sim.step(zero);
        if (r.goal_scored) return false;
        if (sim.state().phase == Phase::MatchOver) break;
    }
    return true;
}

}  // namespace

// ---- v1 generator (audit-only / deprecated) --------------------------------

GameState make_scenario_v1(std::uint64_t seed,
                           std::array<Character, 2> characters,
                           std::int32_t max_ticks) noexcept {
    if (max_ticks <= 0) max_ticks = 60 * kSimHz;

    SplitMix64 rng(seed);

    for (std::int32_t attempt = 0; attempt < kScenarioMaxRetries; ++attempt) {
        GameState s{};
        s.ruleset_version = kRulesetVersion;
        s.tick = 0;
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.regulation_remaining = max_ticks;
        s.golden_goal = false;
        s.score = {0, 0};
        s.last_touch = -1;
        s.config = {1, max_ticks, 0};
        s.players[0].character = characters[0];
        s.players[1].character = characters[1];
        s.players[0].facing = {kFixedScale, 0};
        s.players[1].facing = {-kFixedScale, 0};

        // Player 0 inside the left half, player 1 inside the right half.
        s.players[0].position.x =
            rng.next_i32(kPlayerRadius, kFieldHalfX - kPlayerRadius);
        s.players[0].position.y =
            rng.next_i32(kPlayerRadius, kFieldHeight - kPlayerRadius);
        s.players[1].position.x =
            rng.next_i32(kFieldHalfX + kPlayerRadius,
                         kFieldWidth - kPlayerRadius);
        s.players[1].position.y =
            rng.next_i32(kPlayerRadius, kFieldHeight - kPlayerRadius);

        // Core away from goal lines: keep x in the middle third band.
        s.core.position.x =
            rng.next_i32(kFieldWidth / 4, 3 * kFieldWidth / 4);
        s.core.position.y =
            rng.next_i32(kCoreRadius, kFieldHeight - kCoreRadius);

        // Deterministic velocity with magnitude strictly below kCoreMaxSpeed.
        std::int32_t vx = rng.next_i32(-(kCoreMaxSpeed - 1), kCoreMaxSpeed);
        std::int32_t vy = rng.next_i32(-(kCoreMaxSpeed - 1), kCoreMaxSpeed);
        s.core.velocity = cap_length({vx, vy}, kCoreMaxSpeed - 1);
        if (s.core.velocity.x == 0 && s.core.velocity.y == 0) {
            s.core.velocity = {1, 0};
        }

        if (circles_in_bounds(s) && no_overlap(s)) {
            return s;
        }
    }

    // Documented safe fallback.
    return scenario_fallback(characters, max_ticks);
}

bool scenario_v1_is_valid(const GameState& s) noexcept {
    if (s.ruleset_version != kRulesetVersion) return false;
    if (s.tick != 0) return false;
    if (s.phase != Phase::Live) return false;
    if (s.kickoff_remaining != 0) return false;
    if (s.golden_goal) return false;
    if (s.score[0] != 0 || s.score[1] != 0) return false;
    if (s.last_touch != -1) return false;
    if (s.config.score_to_win != 1) return false;
    if (s.config.kickoff_ticks != 0) return false;
    if (s.config.regulation_ticks <= 0) return false;

    // No active effects and all action/effect timers zero. effect_position is
    // meaningless when no effect is active (mirroring maps {0,0} to a nonzero
    // value), so it is not constrained here.
    for (const auto& p : s.players) {
        if (p.effect_kind != EffectKind::None) return false;
        if (p.effect_ticks != 0) return false;
        if (p.strike_ticks != 0) return false;
        if (p.strike_cooldown != 0) return false;
        if (p.dash_cooldown != 0) return false;
        if (p.ability_cooldown != 0) return false;
        if (p.jetstep_bonus_used) return false;
        if (p.strike_has_hit) return false;
        if (p.velocity.x != 0 || p.velocity.y != 0) return false;
    }

    if (!circles_in_bounds(s)) return false;
    if (!no_overlap(s)) return false;

    // Player 0 inside the left half, player 1 inside the right half.
    if (s.players[0].position.x > kFieldHalfX) return false;
    if (s.players[1].position.x < kFieldHalfX) return false;

    // Core away from goal lines (middle half band).
    if (s.core.position.x < kFieldWidth / 4 ||
        s.core.position.x > 3 * kFieldWidth / 4)
        return false;

    // Core velocity magnitude strictly below kCoreMaxSpeed.
    std::uint64_t speed = isqrt64(static_cast<std::uint64_t>(
        length_sq(s.core.velocity)));
    if (speed >= static_cast<std::uint64_t>(kCoreMaxSpeed)) return false;

    return true;
}

// ---- v2 generator (current) ------------------------------------------------

GameState make_scenario_v2(std::uint64_t seed,
                           std::array<Character, 2> characters,
                           std::int32_t max_ticks) noexcept {
    if (max_ticks <= 0) max_ticks = 60 * kSimHz;

    SplitMix64 rng(seed);

    // Inclusive middle third for core x.
    const std::int32_t mid_lo = kFieldWidth / 3;
    const std::int32_t mid_hi = 2 * kFieldWidth / 3;

    for (std::int32_t attempt = 0; attempt < kScenarioMaxRetriesV2; ++attempt) {
        GameState s{};
        s.ruleset_version = kRulesetVersion;
        s.tick = 0;
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.regulation_remaining = max_ticks;
        s.golden_goal = false;
        s.score = {0, 0};
        s.last_touch = -1;
        s.config = {1, max_ticks, 0};
        s.players[0].character = characters[0];
        s.players[1].character = characters[1];
        s.players[0].facing = {kFixedScale, 0};
        s.players[1].facing = {-kFixedScale, 0};

        // Player 0 inside the left half, player 1 inside the right half.
        s.players[0].position.x =
            rng.next_i32(kPlayerRadius, kFieldHalfX - kPlayerRadius);
        s.players[0].position.y =
            rng.next_i32(kPlayerRadius, kFieldHeight - kPlayerRadius);
        s.players[1].position.x =
            rng.next_i32(kFieldHalfX + kPlayerRadius,
                         kFieldWidth - kPlayerRadius);
        s.players[1].position.y =
            rng.next_i32(kPlayerRadius, kFieldHeight - kPlayerRadius);

        // Core x in the inclusive middle third, y inside bounds.
        s.core.position.x = rng.next_i32(mid_lo, mid_hi + 1);
        s.core.position.y =
            rng.next_i32(kCoreRadius, kFieldHeight - kCoreRadius);

        // Core speed strictly below kStrikeMinForwardSpeed; zero is valid.
        std::int32_t vx = rng.next_i32(-(kStrikeMinForwardSpeed - 1),
                                       kStrikeMinForwardSpeed);
        std::int32_t vy = rng.next_i32(-(kStrikeMinForwardSpeed - 1),
                                       kStrikeMinForwardSpeed);
        s.core.velocity = cap_length({vx, vy}, kStrikeMinForwardSpeed - 1);

        if (circles_in_bounds(s) && no_overlap(s) &&
            min_player_core_sep(s) && idle_safety_probe(s)) {
            return s;
        }
    }

    // Documented safe fallback: quarter-field players, stationary center core.
    return scenario_fallback_v2(characters, max_ticks);
}

bool scenario_v2_is_valid(const GameState& s) noexcept {
    if (s.ruleset_version != kRulesetVersion) return false;
    if (s.tick != 0) return false;
    if (s.phase != Phase::Live) return false;
    if (s.kickoff_remaining != 0) return false;
    if (s.golden_goal) return false;
    if (s.score[0] != 0 || s.score[1] != 0) return false;
    if (s.last_touch != -1) return false;
    if (s.config.score_to_win != 1) return false;
    if (s.config.kickoff_ticks != 0) return false;
    if (s.config.regulation_ticks <= 0) return false;
    if (s.regulation_remaining != s.config.regulation_ticks) return false;

    // No active effects and all action/effect timers zero. Players stationary.
    for (const auto& p : s.players) {
        if (p.effect_kind != EffectKind::None) return false;
        if (p.effect_ticks != 0) return false;
        if (p.strike_ticks != 0) return false;
        if (p.strike_cooldown != 0) return false;
        if (p.dash_cooldown != 0) return false;
        if (p.ability_cooldown != 0) return false;
        if (p.jetstep_bonus_used) return false;
        if (p.strike_has_hit) return false;
        if (p.velocity.x != 0 || p.velocity.y != 0) return false;
    }

    // v2 start-facing invariant: players face inward at generated starts.
    // Player 0 (left, attacks right) faces +x; player 1 (right, attacks left)
    // faces -x. Exact vector match, not just sign.
    if (s.players[0].facing.x != kFixedScale ||
        s.players[0].facing.y != 0)
        return false;
    if (s.players[1].facing.x != -kFixedScale ||
        s.players[1].facing.y != 0)
        return false;

    if (!circles_in_bounds(s)) return false;
    if (!no_overlap(s)) return false;
    if (!min_player_core_sep(s)) return false;

    // Player 0 inside the left half, player 1 inside the right half.
    if (s.players[0].position.x > kFieldHalfX) return false;
    if (s.players[1].position.x < kFieldHalfX) return false;

    // Core x in the inclusive middle third.
    if (s.core.position.x < kFieldWidth / 3 ||
        s.core.position.x > 2 * kFieldWidth / 3)
        return false;

    // Core speed strictly below kStrikeMinForwardSpeed.
    std::uint64_t speed = isqrt64(static_cast<std::uint64_t>(
        length_sq(s.core.velocity)));
    if (speed >= static_cast<std::uint64_t>(kStrikeMinForwardSpeed))
        return false;

    // Idle-safety probe: both players idle for the safety horizon produces no
    // goal. This is checked here so a later generator edit cannot silently
    // weaken it.
    if (!idle_safety_probe(s)) return false;

    return true;
}

}  // namespace pulse::ai
