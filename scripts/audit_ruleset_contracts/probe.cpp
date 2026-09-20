// Synthetic contract probes only: no policy, model, scenario seeds, or training data.
#include "pulse_ai.hpp"
#include "pulse_replay.hpp"
#if AUDIT_RULESET == 2
#include "pulse_core_compat.hpp"
#endif

#include <bit>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

using namespace pulse;
static_assert(kRulesetVersion == AUDIT_RULESET);

namespace {
int touch_kind(const GameState& s) {
#if AUDIT_RULESET == 4
    return static_cast<int>(s.last_touch_kind);
#else
    return static_cast<int>(touch_kind_of(s));
#endif
}
void set_touch(GameState& s, int kind) {
    s.last_touch = kind == 0 ? -1 : 0;
#if AUDIT_RULESET == 4
    s.last_touch_kind = static_cast<CoreTouchKind>(kind);
#endif
}
template<class Values> void array(const Values& values) {
    std::cout << '[';
    bool first = true;
    for (auto value : values) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << +value;
    }
    std::cout << ']';
}
void frame(const FrameInput& f) {
    array(std::array<int, 5>{f.move_x, f.move_y, f.aim_x, f.aim_y, f.buttons});
}
GameState live() {
    GameState s = Simulation{}.state();
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    return s;
}
void emit(const std::string& name, const char* stage, const Simulation& sim,
          const StepResult& result = {}) {
    const auto& s = sim.state();
    std::cout << "{\"type\":\"state\",\"fixture\":\"" << name
              << "\",\"stage\":\"" << stage << "\",\"common\":{\"clock\":";
    array(std::array<int, 7>{static_cast<int>(s.tick), static_cast<int>(s.phase),
        s.kickoff_remaining, s.regulation_remaining, s.golden_goal, s.score[0], s.score[1]});
    std::cout << ",\"config\":";
    array(std::array<int, 3>{s.config.score_to_win, s.config.regulation_ticks, s.config.kickoff_ticks});
    std::cout << ",\"core\":";
    array(std::array<int, 4>{s.core.position.x, s.core.position.y, s.core.velocity.x, s.core.velocity.y});
    std::cout << ",\"last_touch\":" << +s.last_touch << ",\"players\":[";
    for (std::size_t i = 0; i < 2; ++i) {
        if (i) std::cout << ',';
        const auto& p = s.players[i];
        array(std::array<int, 17>{static_cast<int>(p.character), p.position.x, p.position.y,
            p.velocity.x, p.velocity.y, p.facing.x, p.facing.y, p.effect_position.x,
            p.effect_position.y, static_cast<int>(p.effect_kind), p.jetstep_bonus_used,
            p.strike_has_hit, p.strike_ticks, p.strike_cooldown, p.dash_cooldown,
            p.ability_cooldown, p.effect_ticks});
    }
    std::cout << "]},\"touch_kind\":" << touch_kind(s) << ",\"state_hash\":\""
              << sim.state_hash() << "\",\"goal_result\":";
#if AUDIT_RULESET == 4
    const int goal_seat = result.goal_last_touch;
    const int goal_kind = static_cast<int>(result.goal_last_touch_kind);
#else
    const int goal_seat = result.goal_scored ? goal_touch_seat_of(s) : -1;
    const int goal_kind = result.goal_scored ? static_cast<int>(goal_touch_of(s)) : 0;
#endif
    array(std::array<int, 5>{result.goal_scored, result.scoring_team, result.match_finished,
                           goal_seat, goal_kind});
    std::cout << ",\"observation\":[";
    for (std::size_t seat = 0; seat < 2; ++seat) {
        if (seat) std::cout << ',';
        array(ai::encode_observation(s, seat));
    }
    std::cout << "],\"observation_bits\":[";
    for (std::size_t seat = 0; seat < 2; ++seat) {
        if (seat) std::cout << ',';
        const auto obs = ai::encode_observation(s, seat);
        std::array<std::uint32_t, ai::kObservationSize> bits{};
        for (std::size_t i = 0; i < obs.size(); ++i) bits[i] = std::bit_cast<std::uint32_t>(obs[i]);
        array(bits);
    }
    std::cout << "],\"masks\":[";
    for (std::size_t seat = 0; seat < 2; ++seat) {
        if (seat) std::cout << ',';
        const auto mask = ai::make_action_mask(s, seat);
        std::cout << '['; array(mask.move); std::cout << ','; array(mask.aim);
        std::cout << ','; array(mask.button); std::cout << ']';
    }
    std::cout << "]}\n";
}
void sample(const std::string& name, GameState state, bool step = false,
            std::array<FrameInput, 2> inputs = {}) {
    Simulation sim;
    sim.set_state(state);
    emit(name, "before", sim);
    if (step) {
        const auto result = sim.step(inputs);
        emit(name, "after_one_tick", sim, result);
    }
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--write-replay") {
        Simulation sim;
        Replay replay;
        replay.characters = {sim.state().players[0].character, sim.state().players[1].character};
        replay.config = sim.config();
        (void)sim.step({});
        replay.ticks.push_back({{}, sim.state_hash()});
        std::string error;
        if (!save_replay(replay, argv[2], error)) { std::cerr << error; return 1; }
        std::cout << "{\"saved_ticks\":1,\"ruleset\":" << kRulesetVersion << "}\n";
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--read-replay") {
        Replay replay;
        std::string error;
        const bool loaded = load_replay(argv[2], replay, error);
        const bool verified = loaded && verify_replay(replay).ok;
        // The controlled fixture paths exist, so diagnostics contain no path strings.
        std::cout << "{\"loaded\":" << (loaded ? "true" : "false")
                  << ",\"verified\":" << (verified ? "true" : "false")
                  << ",\"ticks\":" << replay.ticks.size() << ",\"error\":\"" << error << "\"}\n";
        return 0;
    }
    if (argc != 1) return 2;
    std::cout << std::setprecision(std::numeric_limits<float>::max_digits10);
    std::cout << "{\"type\":\"metadata\",\"ruleset\":" << kRulesetVersion
              << ",\"replay_format\":" << kReplayFormatVersion
              << ",\"observation_size\":" << ai::kObservationSize
              << ",\"agent_contract\":" << ai::kAgentContractVersion << ",\"physics_constants\":";
    array(std::array<int, 4>{kCoreMaxSpeed, kStrikeImpulse, kStrikeMinForwardSpeed, kKiteStrikeBonus});
    std::cout << "}\n{\"type\":\"actions\",\"values\":[";
    for (int seat = 0; seat < 2; ++seat) {
        for (int a = 0; a < Simulation::kActionSpaceSize; ++a) {
            if (seat || a) std::cout << ',';
            auto input = Simulation::decode_action(a);
            if (seat) input = Simulation::mirror_input(input);
            frame(input);
        }
    }
    std::cout << "]}\n";
    sample("neutral_control", live(), true);
    auto s = live();
    s.core.velocity = {160, -80};
    sample("velocity_normalization", s);
    s.core.velocity = {350, 0};
    sample("free_core_velocity", s, true);
    for (int kind = 0; kind < 5; ++kind) {
        s = live(); set_touch(s, kind);
        sample("synthetic_prior_touch_" + std::to_string(kind), s);
    }
    // All strike/cooldown gating combinations, with reversed flags for seat 1.
    for (int flags = 0; flags < 16; ++flags) {
        s = live();
        for (int seat = 0; seat < 2; ++seat) {
            const int f = seat ? 15 - flags : flags;
            auto& p = s.players[seat];
            p.strike_cooldown = (f & 1) != 0; p.ability_cooldown = (f & 2) != 0;
            p.dash_cooldown = (f & 4) != 0; p.strike_ticks = (f & 8) != 0;
        }
        sample("mask_flags_" + std::to_string(flags), s);
    }
    s = live(); s.phase = Phase::Kickoff; sample("mask_kickoff", s);
    s.phase = Phase::MatchOver; sample("mask_match_over", s);

    // Contact fixtures adapted from reference tests/sim_tests.cpp (touch provenance tests).
    s = live(); s.players[0].facing = {kFixedScale, 0};
    s.core.position = {s.players[0].position.x + kPlayerRadius + kCoreRadius, kFieldHalfY};
    std::array<FrameInput, 2> inputs{}; inputs[0].buttons = ButtonStrike;
    sample("strike", s, true, inputs);
    s.players[0].effect_kind = EffectKind::Jetstep; s.players[0].effect_ticks = 45;
    sample("jetstep_strike", s, true, inputs);
    s = live(); s.core.position = {s.players[0].position.x + kPlayerRadius + kCoreRadius - 100, kFieldHalfY};
    s.core.velocity = {-200, 0}; sample("player_body", s, true);
    s = live(); s.players[0].character = Character::Vale;
    s.players[0].effect_kind = EffectKind::AnchorWell; s.players[0].effect_ticks = 120;
    s.players[0].effect_position = {kFieldWidth / 4 + 4 * kFixedScale, kFieldHalfY};
    s.core.position = {s.players[0].effect_position.x + 2 * kFixedScale, kFieldHalfY};
    sample("anchor_well", s, true);
    s = live(); s.players[0].character = Character::Bastion;
    s.players[0].position = {1000, kFieldHalfY};
    s.players[0].effect_kind = EffectKind::PulseGate; s.players[0].effect_ticks = 120;
    s.players[0].effect_position = {kFieldWidth / 4 + 4 * kFixedScale, kFieldHalfY};
    s.core.position = {s.players[0].effect_position.x - kGateRadius - kCoreRadius - 50, kFieldHalfY};
    s.core.velocity = {300, 0}; sample("pulse_gate", s, true);
    s = live(); set_touch(s, 1);
    s.core.position = {kFieldWidth - 250, kFieldHalfY}; s.core.velocity = {300, 0};
    sample("nonwinning_goal_reset", s, true);
    return 0;
}
