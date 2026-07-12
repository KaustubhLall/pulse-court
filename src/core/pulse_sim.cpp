#include "pulse_sim.hpp"
#include "fixed_math.hpp"

#include <cstring>

namespace pulse {

namespace {

// Normalize a velocity vector to fixed-point unit length.
Vec2 normalize_velocity(Vec2 v) noexcept {
    std::uint64_t len = isqrt64(static_cast<std::uint64_t>(length_sq(v)));
    if (len == 0) return {kFixedScale, 0};
    std::int64_t nx = static_cast<std::int64_t>(v.x) * kFixedScale /
                      static_cast<std::int64_t>(len);
    std::int64_t ny = static_cast<std::int64_t>(v.y) * kFixedScale /
                      static_cast<std::int64_t>(len);
    return {static_cast<std::int32_t>(nx), static_cast<std::int32_t>(ny)};
}

// Push a circle (core) out of an immovable circle and reflect its velocity.
void resolve_core_vs_circle(CoreState& core, Vec2 center,
                            std::int32_t radius) noexcept {
    std::int64_t dx = core.position.x - center.x;
    std::int64_t dy = core.position.y - center.y;
    std::int64_t min_dist = kCoreRadius + radius;
    std::int64_t dist2 = dx * dx + dy * dy;
    std::int64_t min_dist2 = min_dist * min_dist;
    if (dist2 >= min_dist2) return;

    if (dist2 == 0) {
        // Exact overlap fallback: push opposite the core's velocity, or +x if
        // the core is stationary. This is deterministic because velocity is
        // part of the authoritative state.
        Vec2 n = {kFixedScale, 0};
        if (core.velocity.x != 0 || core.velocity.y != 0) {
            n = normalize_velocity({-core.velocity.x, -core.velocity.y});
        }
        core.position.x += static_cast<std::int32_t>(n.x * min_dist /
                                                     kFixedScale);
        core.position.y += static_cast<std::int32_t>(n.y * min_dist /
                                                     kFixedScale);
        core.velocity = reflect(core.velocity, n);
        return;
    }

    std::uint64_t dist = isqrt64(static_cast<std::uint64_t>(dist2));
    std::int64_t overlap = min_dist - static_cast<std::int64_t>(dist);
    if (overlap <= 0) return;

    Vec2 n{static_cast<std::int32_t>(dx * kFixedScale /
                                      static_cast<std::int64_t>(dist)),
           static_cast<std::int32_t>(dy * kFixedScale /
                                      static_cast<std::int64_t>(dist))};
    core.position.x += static_cast<std::int32_t>(n.x * overlap / kFixedScale);
    core.position.y += static_cast<std::int32_t>(n.y * overlap / kFixedScale);
    core.velocity = reflect(core.velocity, n);
}

// Constrain a player inside the court with its radius.
void clamp_player(PlayerState& p) noexcept {
    if (p.position.x < kPlayerRadius) {
        p.position.x = kPlayerRadius;
        if (p.velocity.x < 0) p.velocity.x = 0;
    } else if (p.position.x > kFieldWidth - kPlayerRadius) {
        p.position.x = kFieldWidth - kPlayerRadius;
        if (p.velocity.x > 0) p.velocity.x = 0;
    }
    if (p.position.y < kPlayerRadius) {
        p.position.y = kPlayerRadius;
        if (p.velocity.y < 0) p.velocity.y = 0;
    } else if (p.position.y > kFieldHeight - kPlayerRadius) {
        p.position.y = kFieldHeight - kPlayerRadius;
        if (p.velocity.y > 0) p.velocity.y = 0;
    }
}

// Choose a facing direction from aim if supplied, otherwise movement, else keep.
Vec2 choose_facing(const FrameInput& input, Vec2 current) noexcept {
    if (input.aim_x != 0 || input.aim_y != 0) {
        return normalize_dir(input.aim_x, input.aim_y);
    }
    if (input.move_x != 0 || input.move_y != 0) {
        return normalize_dir(input.move_x, input.move_y);
    }
    return current;
}

}  // namespace

Simulation::Simulation(std::array<Character, 2> characters, MatchConfig config) {
    config_ = config;
    reset(characters);
}

void Simulation::reset(std::array<Character, 2> characters) {
    state_ = GameState{};
    state_.config = config_;
    state_.ruleset_version = kRulesetVersion;
    state_.phase = Phase::Kickoff;
    state_.kickoff_remaining = config_.kickoff_ticks;
    state_.regulation_remaining = config_.regulation_ticks;
    state_.golden_goal = false;
    state_.last_touch = -1;
    state_.players[0].character = characters[0];
    state_.players[1].character = characters[1];
    reset_positions();
}

void Simulation::set_state(const GameState& restored_state) {
    state_ = restored_state;
    config_ = state_.config;
    last_inputs_ = {};
    dash_fired_ = {};
}

const GameState& Simulation::state() const noexcept { return state_; }
const MatchConfig& Simulation::config() const noexcept { return config_; }

void Simulation::reset_positions() {
    state_.core.position = {kFieldHalfX, kFieldHalfY};
    state_.core.velocity = {0, 0};
    state_.last_touch = -1;

    state_.players[0].position = {kFieldWidth / 4, kFieldHalfY};
    state_.players[1].position = {3 * kFieldWidth / 4, kFieldHalfY};

    for (auto& p : state_.players) {
        p.velocity = {0, 0};
        p.effect_kind = EffectKind::None;
        p.effect_ticks = 0;
        p.effect_position = {0, 0};
        p.strike_ticks = 0;
        p.strike_has_hit = false;
        p.jetstep_bonus_used = false;
    }

    state_.players[0].facing = {kFixedScale, 0};
    state_.players[1].facing = {-kFixedScale, 0};
}

void Simulation::decrement_timers() {
    for (auto& p : state_.players) {
        if (p.strike_ticks > 0) {
            --p.strike_ticks;
            if (p.strike_ticks == 0) p.strike_has_hit = false;
        }
        if (p.strike_cooldown > 0) --p.strike_cooldown;
        if (p.dash_cooldown > 0) --p.dash_cooldown;
        if (p.ability_cooldown > 0) --p.ability_cooldown;
        if (p.effect_ticks > 0) {
            --p.effect_ticks;
            if (p.effect_ticks == 0) p.effect_kind = EffectKind::None;
        }
    }
}

StepResult Simulation::step(const std::array<FrameInput, 2>& inputs) {
    StepResult result{};
    ++state_.tick;
    decrement_timers();

    if (state_.phase == Phase::MatchOver) return result;

    dash_fired_ = {};
    last_inputs_ = inputs;

    if (state_.phase == Phase::Kickoff) {
        --state_.kickoff_remaining;
        if (state_.kickoff_remaining <= 0) {
            state_.phase = Phase::Live;
            state_.kickoff_remaining = 0;
        }
        return result;
    }

    for (std::size_t i = 0; i < state_.players.size(); ++i) {
        apply_player_input(static_cast<std::int32_t>(i), inputs[i]);
    }

    resolve_player_motion();
    resolve_strikes();
    apply_effect_forces();

    for (std::int32_t s = 0; s < kCoreSubsteps; ++s) {
        advance_core(result);
        if (result.goal_scored) break;
    }

    if (result.goal_scored) {
        score_goal(result.scoring_team, result);
    }

    update_clock(result);
    return result;
}

void Simulation::apply_player_input(std::int32_t player_index,
                                    FrameInput input) {
    PlayerState& p = state_.players[player_index];
    p.facing = choose_facing(input, p.facing);

    if ((input.buttons & ButtonStrike) && p.strike_cooldown == 0 &&
        p.strike_ticks == 0) {
        p.strike_ticks = kStrikeDuration;
        p.strike_cooldown = kStrikeCooldown;
        p.strike_has_hit = false;
    }

    if ((input.buttons & ButtonDash) && p.dash_cooldown == 0) {
        dash_fired_[player_index] = true;
        p.dash_cooldown = kDashCooldown;
    }

    if ((input.buttons & ButtonAbility) && p.ability_cooldown == 0) {
        p.ability_cooldown = kAbilityCooldown;
        p.jetstep_bonus_used = false;
        p.effect_position = p.position;
        switch (p.character) {
            case Character::Kite:
                p.effect_kind = EffectKind::Jetstep;
                p.effect_ticks = kJetstepDuration;
                break;
            case Character::Vale:
                p.effect_kind = EffectKind::AnchorWell;
                p.effect_ticks = kAnchorDuration;
                p.effect_position =
                    push(p.position, p.facing, kAnchorOffset, kFixedScale);
                break;
            case Character::Bastion:
                p.effect_kind = EffectKind::PulseGate;
                p.effect_ticks = kGateDuration;
                p.effect_position =
                    push(p.position, p.facing, kGateOffset, kFixedScale);
                break;
        }
    }
}

void Simulation::resolve_player_motion() {
    for (std::size_t i = 0; i < state_.players.size(); ++i) {
        PlayerState& p = state_.players[i];
        const FrameInput& in = last_inputs_[i];

        if (dash_fired_[i]) {
            p.velocity.x = p.facing.x * kDashSpeed / kFixedScale;
            p.velocity.y = p.facing.y * kDashSpeed / kFixedScale;
        } else if (in.move_x != 0 || in.move_y != 0) {
            Vec2 dir = normalize_dir(in.move_x, in.move_y);
            p.velocity.x += static_cast<std::int32_t>(dir.x * kPlayerAccel /
                                                      kFixedScale);
            p.velocity.y += static_cast<std::int32_t>(dir.y * kPlayerAccel /
                                                      kFixedScale);
        } else {
            // Friction brings the player to rest when no direction is held.
            std::uint64_t len =
                isqrt64(static_cast<std::uint64_t>(length_sq(p.velocity)));
            if (len <= static_cast<std::uint64_t>(kPlayerFriction)) {
                p.velocity = {0, 0};
            } else {
                p.velocity.x -= static_cast<std::int32_t>(
                    static_cast<std::int64_t>(p.velocity.x) * kPlayerFriction /
                    static_cast<std::int64_t>(len));
                p.velocity.y -= static_cast<std::int32_t>(
                    static_cast<std::int64_t>(p.velocity.y) * kPlayerFriction /
                    static_cast<std::int64_t>(len));
            }
        }

        std::int32_t max_speed = kPlayerMaxSpeed;
        if (p.effect_kind == EffectKind::Jetstep) {
            max_speed += kJetstepSpeedBonus;
        }
        p.velocity = cap_length(p.velocity, max_speed);

        p.position.x += p.velocity.x;
        p.position.y += p.velocity.y;
        clamp_player(p);
    }

    // Player-vs-player collision. Index order is the deterministic tie-breaker
    // for exact overlaps.
    PlayerState& a = state_.players[0];
    PlayerState& b = state_.players[1];
    std::int64_t dx = b.position.x - a.position.x;
    std::int64_t dy = b.position.y - a.position.y;
    std::int64_t min_dist = 2 * kPlayerRadius;
    std::int64_t dist2 = dx * dx + dy * dy;
    std::int64_t min_dist2 = min_dist * min_dist;
    if (dist2 < min_dist2) {
        if (dist2 == 0) {
            // Exact overlap fallback: separate horizontally, reflect x
            // velocity. This is deterministic because players are processed in
            // fixed index order.
            a.position.x -= static_cast<std::int32_t>(min_dist / 2);
            b.position.x += static_cast<std::int32_t>(min_dist / 2);
            clamp_player(a);
            clamp_player(b);
            a.velocity.x = -a.velocity.x;
            b.velocity.x = -b.velocity.x;
        } else {
            std::uint64_t dist =
                isqrt64(static_cast<std::uint64_t>(dist2));
            std::int64_t overlap = min_dist - static_cast<std::int64_t>(dist);
            Vec2 n{static_cast<std::int32_t>(dx * kFixedScale /
                                              static_cast<std::int64_t>(dist)),
                   static_cast<std::int32_t>(dy * kFixedScale /
                                              static_cast<std::int64_t>(dist))};
            std::int64_t shift = overlap / 2;
            a.position.x -=
                static_cast<std::int32_t>(n.x * shift / kFixedScale);
            a.position.y -=
                static_cast<std::int32_t>(n.y * shift / kFixedScale);
            b.position.x +=
                static_cast<std::int32_t>(n.x * shift / kFixedScale);
            b.position.y +=
                static_cast<std::int32_t>(n.y * shift / kFixedScale);
            clamp_player(a);
            clamp_player(b);
            a.velocity = reflect(a.velocity, n);
            b.velocity = reflect(b.velocity, {-n.x, -n.y});
        }
    }
}

void Simulation::resolve_strikes() {
    for (std::size_t i = 0; i < state_.players.size(); ++i) {
        PlayerState& p = state_.players[i];
        if (p.strike_ticks == 0 || p.strike_has_hit) continue;

        std::int64_t dx = state_.core.position.x - p.position.x;
        std::int64_t dy = state_.core.position.y - p.position.y;
        std::int64_t hit_radius = kPlayerRadius + kCoreRadius + kStrikeReach;
        std::int64_t hit_r2 = hit_radius * hit_radius;
        if (dx * dx + dy * dy <= hit_r2) {
            std::int32_t impulse = kStrikeImpulse;
            if (p.character == Character::Kite &&
                p.effect_kind == EffectKind::Jetstep &&
                !p.jetstep_bonus_used) {
                impulse += kKiteStrikeBonus;
                p.jetstep_bonus_used = true;
            }
            state_.core.velocity.x +=
                p.facing.x * impulse / kFixedScale;
            state_.core.velocity.y +=
                p.facing.y * impulse / kFixedScale;
            state_.core.velocity =
                cap_length(state_.core.velocity, kCoreMaxSpeed);
            p.strike_has_hit = true;
            state_.last_touch = static_cast<std::int8_t>(i);
        }
    }
}

void Simulation::apply_effect_forces() {
    for (const auto& p : state_.players) {
        if (p.effect_kind != EffectKind::AnchorWell || p.effect_ticks == 0)
            continue;

        std::int64_t dx = state_.core.position.x - p.effect_position.x;
        std::int64_t dy = state_.core.position.y - p.effect_position.y;
        std::int64_t dist2 = dx * dx + dy * dy;
        std::int64_t radius = kAnchorRadius;
        if (dist2 == 0 || dist2 > radius * radius) continue;

        std::uint64_t dist = isqrt64(static_cast<std::uint64_t>(dist2));
        Vec2 n{static_cast<std::int32_t>(dx * kFixedScale /
                                          static_cast<std::int64_t>(dist)),
               static_cast<std::int32_t>(dy * kFixedScale /
                                          static_cast<std::int64_t>(dist))};
        state_.core.velocity.x -=
            static_cast<std::int32_t>(n.x * kAnchorStrength / kFixedScale);
        state_.core.velocity.y -=
            static_cast<std::int32_t>(n.y * kAnchorStrength / kFixedScale);
    }
    state_.core.velocity = cap_length(state_.core.velocity, kCoreMaxSpeed);
}

void Simulation::advance_core(StepResult& result) {
    if (state_.phase != Phase::Live) return;

    CoreState& core = state_.core;
    core.velocity = cap_length(core.velocity, kCoreMaxSpeed);

    const std::int32_t n = kCoreSubsteps;
    for (std::int32_t s = 0; s < n; ++s) {
        // Integrate by exactly n substeps. The remainder is applied to the
        // first substep so the sum is exact and deterministic for any velocity.
        std::int32_t dx = core.velocity.x / n;
        std::int32_t rx = core.velocity.x - dx * n;
        if (s == 0) dx += rx;

        std::int32_t dy = core.velocity.y / n;
        std::int32_t ry = core.velocity.y - dy * n;
        if (s == 0) dy += ry;

        core.position.x += dx;
        core.position.y += dy;

        // Gate collisions are resolved before player and wall contacts.
        for (const auto& p : state_.players) {
            if (p.effect_kind == EffectKind::PulseGate && p.effect_ticks > 0) {
                resolve_core_vs_circle(core, p.effect_position, kGateRadius);
            }
        }

        // Player collisions are resolved in fixed player order.
        for (const auto& p : state_.players) {
            resolve_core_vs_circle(core, p.position, kPlayerRadius);
        }

        // Goal detection before wall reflection.
        if (core.position.y >= kGoalTop && core.position.y <= kGoalBottom) {
            if (core.position.x <= 0) {
                result.goal_scored = true;
                result.scoring_team = 1;  // right team scores in left goal
                return;
            }
            if (core.position.x >= kFieldWidth) {
                result.goal_scored = true;
                result.scoring_team = 0;  // left team scores in right goal
                return;
            }
        }

        // Side wall bounces are skipped inside the goal mouth y-range so the
        // core can pass through and be scored on the next substep.
        bool in_goal_y =
            core.position.y >= kGoalTop && core.position.y <= kGoalBottom;
        if (!in_goal_y) {
            if (core.position.x < kCoreRadius) {
                core.position.x = 2 * kCoreRadius - core.position.x;
                core.velocity.x = -core.velocity.x;
            } else if (core.position.x > kFieldWidth - kCoreRadius) {
                core.position.x =
                    2 * (kFieldWidth - kCoreRadius) - core.position.x;
                core.velocity.x = -core.velocity.x;
            }
        }

        if (core.position.y < kCoreRadius) {
            core.position.y = 2 * kCoreRadius - core.position.y;
            core.velocity.y = -core.velocity.y;
        } else if (core.position.y > kFieldHeight - kCoreRadius) {
            core.position.y =
                2 * (kFieldHeight - kCoreRadius) - core.position.y;
            core.velocity.y = -core.velocity.y;
        }
    }
}

void Simulation::score_goal(std::int32_t team, StepResult& result) {
    if (team < 0 || team > 1) return;
    state_.score[team] += 1;
    bool winning = (state_.score[team] >= config_.score_to_win) ||
                   state_.golden_goal;
    if (winning) {
        state_.phase = Phase::MatchOver;
        result.match_finished = true;
    } else {
        state_.phase = Phase::Kickoff;
        state_.kickoff_remaining = config_.kickoff_ticks;
        reset_positions();
    }
}

void Simulation::update_clock(StepResult& result) {
    if (state_.phase != Phase::Live) return;
    if (state_.regulation_remaining > 0) {
        --state_.regulation_remaining;
        if (state_.regulation_remaining == 0) {
            if (state_.score[0] != state_.score[1]) {
                state_.phase = Phase::MatchOver;
                result.match_finished = true;
            } else {
                state_.golden_goal = true;
            }
        }
    }
}

FrameInput Simulation::decode_action(std::int32_t action_index) noexcept {
    std::int32_t a = action_index % kActionSpaceSize;
    if (a < 0) a += kActionSpaceSize;
    std::int32_t move_dir = a % 9;
    std::int32_t aim_dir = (a / 9) % 9;
    std::int32_t button = a / 81;

    auto map_dir = [](std::int32_t d) -> std::pair<std::int8_t, std::int8_t> {
        switch (d) {
            case 0: return {0, 0};
            case 1: return {0, 1};
            case 2: return {0, -1};
            case 3: return {-1, 0};
            case 4: return {1, 0};
            case 5: return {-1, 1};
            case 6: return {1, 1};
            case 7: return {-1, -1};
            default: return {1, -1};  // d == 8
        }
    };

    FrameInput in;
    auto [mx, my] = map_dir(move_dir);
    in.move_x = mx;
    in.move_y = my;
    auto [ax, ay] = map_dir(aim_dir);
    in.aim_x = ax;
    in.aim_y = ay;

    switch (button) {
        case 0: in.buttons = ButtonNone; break;
        case 1: in.buttons = ButtonStrike; break;
        case 2: in.buttons = ButtonAbility; break;
        default: in.buttons = ButtonDash; break;
    }
    return in;
}

FrameInput Simulation::mirror_input(const FrameInput& input) noexcept {
    FrameInput out = input;
    out.move_x = -out.move_x;
    out.aim_x = -out.aim_x;
    return out;
}

GameState Simulation::mirror_state(const GameState& state) noexcept {
    GameState m = state;
    m.score = {state.score[1], state.score[0]};
    m.core = state.core;
    m.core.position.x = kFieldWidth - state.core.position.x;
    m.core.velocity.x = -state.core.velocity.x;

    if (state.last_touch == 0)
        m.last_touch = 1;
    else if (state.last_touch == 1)
        m.last_touch = 0;
    else
        m.last_touch = -1;

    auto mirror_player = [](const PlayerState& p) {
        PlayerState r = p;
        r.position.x = kFieldWidth - p.position.x;
        r.velocity.x = -p.velocity.x;
        r.facing.x = -p.facing.x;
        r.effect_position.x = kFieldWidth - p.effect_position.x;
        return r;
    };

    m.players[0] = mirror_player(state.players[1]);
    m.players[1] = mirror_player(state.players[0]);
    return m;
}

namespace {

void hash_u64(std::uint64_t& h, std::uint64_t v) noexcept {
    for (std::size_t i = 0; i < sizeof(v); ++i) {
        h ^= (v >> (i * 8)) & 0xffULL;
        h *= 1099511628211ULL;
    }
}

void hash_i32(std::uint64_t& h, std::int32_t v) noexcept {
    hash_u64(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(v)));
}

void hash_u32(std::uint64_t& h, std::uint32_t v) noexcept {
    hash_u64(h, static_cast<std::uint64_t>(v));
}

void hash_i8(std::uint64_t& h, std::int8_t v) noexcept {
    hash_u64(h, static_cast<std::uint64_t>(static_cast<std::uint8_t>(v)));
}

void hash_bool(std::uint64_t& h, bool v) noexcept {
    hash_u64(h, v ? 1ULL : 0ULL);
}

}  // namespace

std::uint64_t Simulation::state_hash() const noexcept {
    std::uint64_t h = 14695981039346656037ULL;  // FNV-1a offset basis
    const GameState& s = state_;

    hash_u32(h, s.ruleset_version);
    hash_u32(h, s.tick);
    hash_u64(h, static_cast<std::uint64_t>(s.phase));
    hash_i32(h, s.kickoff_remaining);
    hash_i32(h, s.regulation_remaining);
    hash_bool(h, s.golden_goal);
    hash_i32(h, s.score[0]);
    hash_i32(h, s.score[1]);

    for (const auto& p : s.players) {
        hash_u64(h, static_cast<std::uint64_t>(p.character));
        hash_i32(h, p.position.x);
        hash_i32(h, p.position.y);
        hash_i32(h, p.velocity.x);
        hash_i32(h, p.velocity.y);
        hash_i32(h, p.facing.x);
        hash_i32(h, p.facing.y);
        hash_i32(h, p.effect_position.x);
        hash_i32(h, p.effect_position.y);
        hash_u64(h, static_cast<std::uint64_t>(p.effect_kind));
        hash_bool(h, p.jetstep_bonus_used);
        hash_bool(h, p.strike_has_hit);
        hash_i32(h, p.strike_ticks);
        hash_i32(h, p.strike_cooldown);
        hash_i32(h, p.dash_cooldown);
        hash_i32(h, p.ability_cooldown);
        hash_i32(h, p.effect_ticks);
    }

    hash_i32(h, s.core.position.x);
    hash_i32(h, s.core.position.y);
    hash_i32(h, s.core.velocity.x);
    hash_i32(h, s.core.velocity.y);
    hash_i8(h, s.last_touch);

    hash_i32(h, s.config.score_to_win);
    hash_i32(h, s.config.regulation_ticks);
    hash_i32(h, s.config.kickoff_ticks);

    return h;
}

const char* character_name(Character character) noexcept {
    switch (character) {
        case Character::Kite: return "kite";
        case Character::Vale: return "vale";
        case Character::Bastion: return "bastion";
    }
    return "unknown";
}

bool parse_character(const char* text, Character& character) noexcept {
    if (!text) return false;
    if (std::strcmp(text, "kite") == 0 || std::strcmp(text, "Kite") == 0) {
        character = Character::Kite;
        return true;
    }
    if (std::strcmp(text, "vale") == 0 || std::strcmp(text, "Vale") == 0) {
        character = Character::Vale;
        return true;
    }
    if (std::strcmp(text, "bastion") == 0 ||
        std::strcmp(text, "Bastion") == 0) {
        character = Character::Bastion;
        return true;
    }
    return false;
}

}  // namespace pulse
