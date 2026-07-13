#include "fixed_math.hpp"
#include "pulse_replay.hpp"
#include "pulse_sim.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace pulse {
namespace tests {

static int g_passed = 0;
static int g_failed = 0;

void check(bool cond, const char* name) {
    if (cond) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "FAIL: " << name << "\n";
    }
}

void report() {
    std::cout << "Tests: " << g_passed << " passed, " << g_failed
              << " failed\n";
    if (g_failed > 0) std::exit(1);
}

// Deterministic scripted tape used for repeatability tests.
FrameInput scripted(std::uint32_t tick, std::size_t player) {
    std::uint32_t t = tick + static_cast<std::uint32_t>(player) * 1000u;
    std::int32_t move = static_cast<std::int32_t>((t / 30u) % 8u) + 1;
    auto decode_dir = [](std::int32_t d) -> std::pair<std::int8_t, std::int8_t> {
        switch (d % 9) {
            case 0: return {0, 0};
            case 1: return {0, 1};
            case 2: return {0, -1};
            case 3: return {-1, 0};
            case 4: return {1, 0};
            case 5: return {-1, 1};
            case 6: return {1, 1};
            case 7: return {-1, -1};
            default: return {1, -1};
        }
    };
    auto [mx, my] = decode_dir(move);
    FrameInput in;
    in.move_x = mx;
    in.move_y = my;
    in.aim_x = mx;
    in.aim_y = my;
    if ((t % 180u) == 0u) in.buttons = ButtonStrike;
    if ((t % 350u) == 50u) in.buttons = ButtonAbility;
    if ((t % 220u) == 25u) in.buttons = ButtonDash;
    return in;
}

void test_fixed_math() {
    Vec2 n0 = normalize_dir(1, 0);
    check(n0.x == kFixedScale && n0.y == 0, "normalize (1,0)");

    Vec2 n1 = normalize_dir(1, 1);
    check(n1.x > 720 && n1.x < 730 && n1.y > 720 && n1.y < 730,
          "normalize (1,1) approximates 0.707");

    Vec2 n2 = normalize_dir(0, 0);
    check(n2.x == 0 && n2.y == 0, "normalize zero returns zero");

    Vec2 c0 = cap_length({2000, 0}, 1000);
    check(c0.x == 1000 && c0.y == 0, "cap length straight");

    Vec2 c1 = cap_length({724, 724}, 1000);
    std::uint64_t len1 =
        isqrt64(static_cast<std::uint64_t>(length_sq(c1)));
    check(len1 <= 1000, "cap length diagonal");

    Vec2 c2 = cap_length({0, 0}, 100);
    check(c2.x == 0 && c2.y == 0, "cap length of zero");

    // Exact overlap fallback through simulation.
    Simulation sim;
    GameState s = sim.state();
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    s.core.position = {kFieldHalfX, kFieldHalfY};
    s.core.velocity = {0, 0};
    s.players[0].position = {kFieldHalfX, kFieldHalfY};
    s.players[0].velocity = {0, 0};
    s.players[0].facing = {kFixedScale, 0};
    sim.set_state(s);
    sim.step({});
    std::int64_t dx = sim.state().core.position.x - sim.state().players[0].position.x;
    std::int64_t dy = sim.state().core.position.y - sim.state().players[0].position.y;
    std::int64_t dist2 = dx * dx + dy * dy;
    std::int64_t min_dist = kPlayerRadius + kCoreRadius;
    check(dist2 >= min_dist * min_dist,
          "exact core-player overlap fallback pushes core out");
}

void test_determinism() {
    const std::int32_t N = 100000;
    Simulation sim({Character::Kite, Character::Bastion});
    std::vector<std::uint64_t> hashes;
    hashes.reserve(N);
    for (std::int32_t i = 0; i < N; ++i) {
        std::array<FrameInput, 2> in = {scripted(static_cast<std::uint32_t>(i), 0),
                                        scripted(static_cast<std::uint32_t>(i), 1)};
        sim.step(in);
        hashes.push_back(sim.state_hash());
    }

    Simulation sim2({Character::Kite, Character::Bastion});
    for (std::int32_t i = 0; i < N; ++i) {
        std::array<FrameInput, 2> in = {scripted(static_cast<std::uint32_t>(i), 0),
                                        scripted(static_cast<std::uint32_t>(i), 1)};
        sim2.step(in);
        check(sim2.state_hash() == hashes[i],
              "deterministic hash repeatability");
    }
}

void test_state_restore() {
    const std::int32_t N1 = 1000;
    const std::int32_t N2 = 500;
    Simulation sim({Character::Vale, Character::Kite});
    std::vector<std::uint64_t> hashes;
    GameState saved;
    for (std::int32_t i = 0; i < N1 + N2; ++i) {
        std::array<FrameInput, 2> in = {scripted(static_cast<std::uint32_t>(i), 0),
                                        scripted(static_cast<std::uint32_t>(i), 1)};
        sim.step(in);
        if (i == N1 - 1) saved = sim.state();
        if (i >= N1) hashes.push_back(sim.state_hash());
    }

    Simulation sim2({Character::Vale, Character::Kite});
    sim2.set_state(saved);
    for (std::int32_t i = 0; i < N2; ++i) {
        std::array<FrameInput, 2> in = {
            scripted(static_cast<std::uint32_t>(N1 + i), 0),
            scripted(static_cast<std::uint32_t>(N1 + i), 1)};
        sim2.step(in);
        check(sim2.state_hash() == hashes[i],
              "restored state continues identically");
    }
}

void test_mirror_identity() {
    Simulation sim({Character::Kite, Character::Vale});
    for (std::int32_t i = 0; i < 2000; ++i) {
        std::array<FrameInput, 2> in = {scripted(static_cast<std::uint32_t>(i), 0),
                                        scripted(static_cast<std::uint32_t>(i), 1)};
        sim.step(in);
    }
    GameState original = sim.state();
    GameState once = Simulation::mirror_state(original);
    GameState twice = Simulation::mirror_state(once);
    check(twice == original, "mirror twice restores exact state");
    check(Simulation::mirror_state(Simulation::mirror_state(original)) ==
              original,
          "mirror twice identity (direct)");
}

void test_goal_scores() {
    Simulation sim;
    GameState s = sim.state();
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    s.core.position = {kFieldWidth - 250, kFieldHalfY};
    s.core.velocity = {300, 0};
    sim.set_state(s);
    StepResult r = sim.step({});
    check(r.goal_scored && r.scoring_team == 0, "goal into right goal scores");
    check(sim.state().score[0] == 1 && sim.state().score[1] == 0,
          "score increments once");
    check(sim.state().phase == Phase::Kickoff, "non-winning goal -> kickoff");
    check(sim.state().kickoff_remaining == sim.config().kickoff_ticks,
          "kickoff timer set");
}

void test_core_contact_and_substeps() {
    // The core uses two internal substeps, but its velocity is integrated once
    // per simulation tick. This guards against accidentally nesting the
    // substep loop at the caller and doubling core travel.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.core.position = {kFieldHalfX, kFieldHalfY};
        s.core.velocity = {200, 0};
        sim.set_state(s);
        (void)sim.step({});
        check(sim.state().core.position.x == kFieldHalfX + 200,
              "core integrates velocity once per tick across substeps");
    }

    // A separating core must be depenetrated without being reflected back into
    // the player on the next contact pass.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
        s.players[0].velocity = {0, 0};
        s.core.position = {s.players[0].position.x + kPlayerRadius +
                               kCoreRadius - 100,
                           s.players[0].position.y};
        s.core.velocity = {100, 0};
        sim.set_state(s);
        (void)sim.step({});
        check(sim.state().core.velocity.x > 0,
              "separating core is not reflected back into player");
    }
}

void test_wall_bounce() {
    Simulation sim;
    GameState s = sim.state();
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    s.core.position = {kFieldWidth - kCoreRadius - 50, 1000};
    s.core.velocity = {200, 0};
    sim.set_state(s);
    StepResult r = sim.step({});
    check(!r.goal_scored, "outside goal mouth does not score");
    check(sim.state().core.velocity.x < 0, "core reflects off side wall");
    check(sim.state().core.position.x <= kFieldWidth - kCoreRadius,
          "core stays within right wall");
    check(sim.state().score[0] == 0 && sim.state().score[1] == 0,
          "score unchanged after wall bounce");
}

void test_wall_bounce_no_false_event() {
    // Core starts slightly outside a non-goal side wall but is already moving
    // inward. Position should be corrected, but velocity should be preserved
    // and no CoreBounce event should be emitted.
    Simulation sim;
    GameState s = sim.state();
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    s.core.position = {kCoreRadius - 10, 1000};  // slightly past left wall
    s.core.velocity = {100, 0};  // moving inward (rightward)
    sim.set_state(s);
    StepEvents events{};
    StepResult r = sim.step({}, &events);
    check(sim.state().core.velocity.x > 0,
          "inward velocity is preserved when core starts outside wall");
    check(sim.state().core.position.x >= kCoreRadius,
          "core position is corrected to legal court");
    check(events.count == 0,
          "no CoreBounce event when core is already moving inward");
}

void test_no_tunneling() {
    Simulation sim;
    GameState s = sim.state();
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    s.core.position = {kFieldWidth - kCoreRadius - 10, 1000};
    s.core.velocity = {5000, 0};
    sim.set_state(s);
    sim.step({});
    std::uint64_t speed =
        isqrt64(static_cast<std::uint64_t>(length_sq(sim.state().core.velocity)));
    check(speed <= static_cast<std::uint64_t>(kCoreMaxSpeed),
          "core speed is capped");
    check(sim.state().core.position.x >= kCoreRadius &&
              sim.state().core.position.x <= kFieldWidth - kCoreRadius,
          "core remains inside side walls");
    check(sim.state().core.position.y >= kCoreRadius &&
              sim.state().core.position.y <= kFieldHeight - kCoreRadius,
          "core remains inside top/bottom walls");
}

void test_strike_and_dash() {
    // One-hit behavior.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
        s.players[0].facing = {kFixedScale, 0};
        s.players[0].strike_cooldown = 0;
        s.players[0].strike_ticks = 0;
        s.core.position = {s.players[0].position.x + kPlayerRadius + kCoreRadius,
                           s.players[0].position.y};
        s.core.velocity = {0, 0};
        sim.set_state(s);
        std::array<FrameInput, 2> in = {};
        in[0].buttons = ButtonStrike;
        sim.step(in);
        std::int64_t vx_after = sim.state().core.velocity.x;
        check(vx_after > 0, "strike applies impulse");
        check(sim.state().players[0].strike_has_hit,
              "strike records hit after contact");

        // Keep core in range while strike is still active; it must not hit
        // again this swing.
        for (int i = 0; i < 6; ++i) {
            GameState s2 = sim.state();
            s2.core.position = {s2.players[0].position.x + kPlayerRadius + kCoreRadius,
                                s2.players[0].position.y};
            s2.core.velocity = {0, 0};
            sim.set_state(s2);
            sim.step({});  // no strike input, strike_ticks still active
        }
        check(sim.state().core.velocity.x == 0,
              "strike one-hit: no further impulse");
    }

    // Dash cooldown.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
        s.players[0].facing = {kFixedScale, 0};
        s.players[0].dash_cooldown = 0;
        sim.set_state(s);
        std::array<FrameInput, 2> in = {};
        in[0].buttons = ButtonDash;
        sim.step(in);
        check(sim.state().players[0].dash_cooldown == kDashCooldown,
              "dash sets cooldown");
        std::uint64_t speed1 =
            isqrt64(static_cast<std::uint64_t>(
                length_sq(sim.state().players[0].velocity)));
        check(speed1 > 0, "dash produces velocity");
        check(speed1 >= static_cast<std::uint64_t>(kDashSpeed - 1),
              "dash is not capped back to normal movement speed");

        // Hold dash while on cooldown; cooldown should tick down, not reset.
        sim.step(in);
        check(sim.state().players[0].dash_cooldown == kDashCooldown - 1,
              "dash cooldown prevents re-fire");
    }

    // Match termination.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.score = {4, 0};
        s.core.position = {kFieldWidth - 250, kFieldHalfY};
        s.core.velocity = {300, 0};
        sim.set_state(s);
        StepResult r = sim.step({});
        check(r.goal_scored && r.match_finished,
              "fifth goal terminates match");
        check(sim.state().score[0] == 5, "score reaches five");
        check(sim.state().phase == Phase::MatchOver, "phase is MatchOver");
    }
}

void test_abilities() {
    auto test_activation = [](Character c, EffectKind expected,
                              std::int32_t expected_cooldown) {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].character = c;
        s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
        s.players[0].facing = {kFixedScale, 0};
        s.players[0].ability_cooldown = 0;
        sim.set_state(s);
        std::array<FrameInput, 2> in = {};
        in[0].buttons = ButtonAbility;
        sim.step(in);
        check(sim.state().players[0].effect_kind == expected,
              "ability activation sets effect");
        check(sim.state().players[0].ability_cooldown == expected_cooldown,
              "ability sets cooldown");
        check(sim.state().players[0].effect_ticks > 0,
              "ability sets effect duration");
    };

    test_activation(Character::Kite, EffectKind::Jetstep, kJetstepCooldown);
    test_activation(Character::Vale, EffectKind::AnchorWell, kAnchorCooldown);
    test_activation(Character::Bastion, EffectKind::PulseGate, kGateCooldown);

    // Cooldown gate.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].character = Character::Kite;
        s.players[0].ability_cooldown = 2;
        sim.set_state(s);
        std::array<FrameInput, 2> in = {};
        in[0].buttons = ButtonAbility;
        sim.step(in);
        check(sim.state().players[0].effect_kind == EffectKind::None,
              "ability blocked by cooldown");
    }

    // Effect expiry.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].character = Character::Vale;
        s.players[0].effect_kind = EffectKind::AnchorWell;
        s.players[0].effect_ticks = 1;
        s.players[0].ability_cooldown = 0;
        sim.set_state(s);
        sim.step({});
        check(sim.state().players[0].effect_kind == EffectKind::None,
              "ability effect expires");
    }

    // Bastion gate bounce.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].character = Character::Bastion;
        s.players[0].position = {1000, kFieldHalfY};
        s.players[0].facing = {kFixedScale, 0};
        s.players[0].effect_kind = EffectKind::PulseGate;
        s.players[0].effect_ticks = 120;
        s.players[0].effect_position = {kFieldWidth / 4 + 4 * kFixedScale,
                                        kFieldHalfY};
        s.core.position = {s.players[0].effect_position.x - kGateRadius - kCoreRadius - 50,
                           kFieldHalfY};
        s.core.velocity = {300, 0};
        sim.set_state(s);
        for (int i = 0; i < 12; ++i) (void)sim.step({});
        check(sim.state().core.velocity.x < 0,
              "Bastion gate reflects core");
    }

    // A strike is directional: a nearby core behind the striker should not
    // be launched through the player.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
        s.players[0].facing = {kFixedScale, 0};
        s.players[0].strike_cooldown = 0;
        s.core.position = {s.players[0].position.x - kPlayerRadius -
                               kCoreRadius,
                           s.players[0].position.y};
        s.core.velocity = {0, 0};
        sim.set_state(s);
        std::array<FrameInput, 2> in = {};
        in[0].buttons = ButtonStrike;
        (void)sim.step(in);
        check(!sim.state().players[0].strike_has_hit &&
                  sim.state().core.velocity.x == 0,
              "strike ignores a core behind the striker");
    }

    // Vale anchor pull.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].character = Character::Vale;
        s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
        s.players[0].facing = {kFixedScale, 0};
        s.players[0].effect_kind = EffectKind::AnchorWell;
        s.players[0].effect_ticks = 120;
        s.players[0].effect_position = {kFieldWidth / 4 + 4 * kFixedScale,
                                        kFieldHalfY};
        s.core.position = {s.players[0].effect_position.x + 2 * kFixedScale,
                           kFieldHalfY};
        s.core.velocity = {0, 0};
        sim.set_state(s);
        sim.step({});
        check(sim.state().core.velocity.x < 0,
              "Vale anchor pulls core toward anchor");
    }

    // Kite jetstep strike bonus.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].character = Character::Kite;
        s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
        s.players[0].facing = {kFixedScale, 0};
        s.players[0].effect_kind = EffectKind::Jetstep;
        s.players[0].effect_ticks = 45;
        s.players[0].strike_cooldown = 0;
        s.core.position = {s.players[0].position.x + kPlayerRadius + kCoreRadius,
                           s.players[0].position.y};
        s.core.velocity = {0, 0};
        sim.set_state(s);
        std::array<FrameInput, 2> in = {};
        in[0].buttons = ButtonStrike;
        sim.step(in);
        std::int64_t vx_kite = sim.state().core.velocity.x;

        Simulation sim2;
        GameState s2 = sim2.state();
        s2.phase = Phase::Live;
        s2.kickoff_remaining = 0;
        s2.players[0].character = Character::Kite;
        s2.players[0].position = s.players[0].position;
        s2.players[0].facing = {kFixedScale, 0};
        s2.players[0].strike_cooldown = 0;
        s2.core.position = s.core.position;
        s2.core.velocity = {0, 0};
        sim2.set_state(s2);
        sim2.step(in);
        std::int64_t vx_base = sim2.state().core.velocity.x;
        check(vx_kite > vx_base, "Kite jetstep adds strike bonus");
    }
}

void test_action_space() {
    for (std::int32_t a = 0; a < Simulation::kActionSpaceSize; ++a) {
        FrameInput in = Simulation::decode_action(a);
        check(in.move_x >= -1 && in.move_x <= 1, "move_x bounded");
        check(in.move_y >= -1 && in.move_y <= 1, "move_y bounded");
        check(in.aim_x >= -1 && in.aim_x <= 1, "aim_x bounded");
        check(in.aim_y >= -1 && in.aim_y <= 1, "aim_y bounded");
        check(in.buttons == ButtonNone || in.buttons == ButtonStrike ||
                  in.buttons == ButtonAbility || in.buttons == ButtonDash,
              "button is one of four commands");
    }
    // Mirror of a decoded action flips x and stays bounded.
    FrameInput in = Simulation::decode_action(123);
    FrameInput m = Simulation::mirror_input(in);
    check(m.move_x == -in.move_x && m.aim_x == -in.aim_x,
          "mirror_input flips x");
}

void test_replay() {
    const std::string path = "test_replay.pulse";
    std::remove(path.c_str());

    // Record a short deterministic replay.
    Simulation sim({Character::Bastion, Character::Vale});
    Replay replay;
    replay.characters = sim.state().players[0].character == Character::Bastion
                            ? std::array{Character::Bastion, Character::Vale}
                            : std::array{Character::Vale, Character::Bastion};
    replay.config = sim.config();
    for (std::int32_t i = 0; i < 500; ++i) {
        std::array<FrameInput, 2> in = {scripted(static_cast<std::uint32_t>(i), 0),
                                        scripted(static_cast<std::uint32_t>(i), 1)};
        sim.step(in);
        RecordedTick rt;
        rt.inputs = in;
        rt.expected_hash = sim.state_hash();
        replay.ticks.push_back(rt);
    }

    std::string error;
    check(save_replay(replay, path, error), "save_replay succeeds");

    Replay loaded;
    check(load_replay(path, loaded, error), "load_replay succeeds");
    check(loaded.ticks.size() == replay.ticks.size(),
          "loaded replay has same tick count");

    ReplayVerification v = verify_replay(loaded);
    check(v.ok, "verify_replay succeeds");

    // Corrupt an input and expect verification failure.
    loaded.ticks[100].inputs[0].buttons = ButtonAbility;
    ReplayVerification bad = verify_replay(loaded);
    check(!bad.ok, "corrupted replay fails verification");

    // Incompatible format version is rejected on load.
    {
        std::ofstream badfile("bad_replay.pulse", std::ios::binary);
        char magic[8] = {'P', 'U', 'L', 'S', 'E', 'R', 'P', 'L'};
        badfile.write(magic, 8);
        std::uint32_t version = 9999;
        badfile.write(reinterpret_cast<const char*>(&version), sizeof(version));
        badfile.write("pad", 3);
        badfile.close();
        Replay dummy;
        std::string err;
        check(!load_replay("bad_replay.pulse", dummy, err),
              "incompatible replay version rejected");
        std::remove("bad_replay.pulse");
    }

    std::remove(path.c_str());
}

void test_court_geometry() {
    // Verify 38x22 court dimensions and goal mouth positions.
    check(kFieldWidth == 38 * kFixedScale, "field width is 38 units");
    check(kFieldHeight == 22 * kFixedScale, "field height is 22 units");
    check(kGoalTop == 8 * kFixedScale, "goal top is y=8");
    check(kGoalBottom == 14 * kFixedScale, "goal bottom is y=14");

    // Verify symmetric reset positions.
    Simulation sim;
    GameState s = sim.state();
    check(s.core.position.x == kFieldHalfX, "core resets at center x");
    check(s.core.position.y == kFieldHalfY, "core resets at center y");
    check(s.players[0].position.x == kFieldWidth / 4,
          "player 0 resets at quarter width");
    check(s.players[0].position.y == kFieldHalfY,
          "player 0 resets at center y");
    check(s.players[1].position.x == 3 * kFieldWidth / 4,
          "player 1 resets at three-quarter width");
    check(s.players[1].position.y == kFieldHalfY,
          "player 1 resets at center y");
}

void test_decision_boundary() {
    // 10 Hz decision boundary: tick 0 is false, 12/24 are true.
    check(!is_decision_tick(0), "tick 0 is not a decision boundary");
    check(is_decision_tick(12), "tick 12 is a decision boundary");
    check(is_decision_tick(24), "tick 24 is a decision boundary");
    check(!is_decision_tick(6), "tick 6 is not a decision boundary");
    check(!is_decision_tick(18), "tick 18 is not a decision boundary");

    // Verify StepEvents populates decision_boundary correctly.
    Simulation sim;
    GameState s = sim.state();
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    sim.set_state(s);

    StepEvents events;
    sim.step({}, &events);
    check(events.tick == 1, "events tick is 1");
    check(!events.decision_boundary, "tick 1 has no decision boundary");

    // Advance to tick 12 and capture events at tick 12.
    for (int i = 0; i < 10; ++i) sim.step({});
    sim.step({}, &events);
    check(events.tick == 12, "events tick is 12");
    check(events.decision_boundary, "tick 12 has decision boundary");
}

void test_action_events() {
    // Test StrikeStarted, DashStarted, AbilityActivated events.
    Simulation sim;
    GameState s = sim.state();
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
    s.players[0].facing = {kFixedScale, 0};
    s.players[0].strike_cooldown = 0;
    s.players[0].dash_cooldown = 0;
    s.players[0].ability_cooldown = 0;
    sim.set_state(s);

    // StrikeStarted event.
    StepEvents events;
    std::array<FrameInput, 2> in = {};
    in[0].buttons = ButtonStrike;
    sim.step(in, &events);
    check(events.count == 1, "strike emits one event");
    check(events.events[0].type == SimulationEventType::StrikeStarted,
          "strike emits StrikeStarted");
    check(events.events[0].actor == 0, "strike event has correct actor");

    // DashStarted event.
    s = sim.state();
    s.players[0].dash_cooldown = 0;
    sim.set_state(s);
    events = StepEvents{};
    in[0].buttons = ButtonDash;
    sim.step(in, &events);
    check(events.count == 1, "dash emits one event");
    check(events.events[0].type == SimulationEventType::DashStarted,
          "dash emits DashStarted");
    check(events.events[0].actor == 0, "dash event has correct actor");

    // AbilityActivated events for all three characters.
    for (Character c : {Character::Kite, Character::Vale, Character::Bastion}) {
        Simulation sim2;
        GameState s2 = sim2.state();
        s2.phase = Phase::Live;
        s2.kickoff_remaining = 0;
        s2.players[0].character = c;
        s2.players[0].position = {kFieldWidth / 4, kFieldHalfY};
        s2.players[0].facing = {kFixedScale, 0};
        s2.players[0].ability_cooldown = 0;
        sim2.set_state(s2);
        events = StepEvents{};
        in[0].buttons = ButtonAbility;
        sim2.step(in, &events);
        check(events.count == 1, "ability emits one event");
        check(events.events[0].type == SimulationEventType::AbilityActivated,
              "ability emits AbilityActivated");
        check(events.events[0].actor == 0, "ability event has correct actor");
    }

    // Cooldown-blocked ability produces no AbilityActivated event.
    {
        Simulation sim2;
        GameState s2 = sim2.state();
        s2.phase = Phase::Live;
        s2.kickoff_remaining = 0;
        s2.players[0].character = Character::Kite;
        s2.players[0].ability_cooldown = 2;
        sim2.set_state(s2);
        events = StepEvents{};
        in[0].buttons = ButtonAbility;
        sim2.step(in, &events);
        check(events.count == 0, "cooldown-blocked ability emits no event");
    }
}

void test_strike_hit_event() {
    Simulation sim;
    GameState s = sim.state();
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
    s.players[0].facing = {kFixedScale, 0};
    s.players[0].strike_cooldown = 0;
    s.players[0].strike_ticks = 0;
    s.core.position = {s.players[0].position.x + kPlayerRadius + kCoreRadius,
                       s.players[0].position.y};
    s.core.velocity = {0, 0};
    sim.set_state(s);

    StepEvents events;
    std::array<FrameInput, 2> in = {};
    in[0].buttons = ButtonStrike;
    sim.step(in, &events);

    // Should have StrikeStarted and StrikeHit.
    check(events.count == 2, "strike hit emits two events");
    check(events.events[0].type == SimulationEventType::StrikeStarted,
          "first event is StrikeStarted");
    check(events.events[1].type == SimulationEventType::StrikeHit,
          "second event is StrikeHit");
    check(events.events[1].actor == 0, "StrikeHit has correct actor");
}

void test_core_bounce_events() {
    // World wall bounce.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.core.position = {kFieldWidth - kCoreRadius - 50, 1000};
        s.core.velocity = {200, 0};
        sim.set_state(s);
        StepEvents events;
        sim.step({}, &events);
        check(events.count == 1, "wall bounce emits one event");
        check(events.events[0].type == SimulationEventType::CoreBounce,
              "wall bounce emits CoreBounce");
        check(events.events[0].actor == -1, "world bounce has actor -1");
    }

    // Player bounce.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
        s.players[0].velocity = {0, 0};
        s.core.position = {s.players[0].position.x + kPlayerRadius +
                               kCoreRadius - 100,
                           s.players[0].position.y};
        s.core.velocity = {-200, 0};
        sim.set_state(s);
        StepEvents events;
        sim.step({}, &events);
        check(events.count == 1, "player bounce emits one event");
        check(events.events[0].type == SimulationEventType::CoreBounce,
              "player bounce emits CoreBounce");
        check(events.events[0].actor == 0, "player bounce has correct actor");
    }

    // Gate bounce.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].character = Character::Bastion;
        s.players[0].position = {1000, kFieldHalfY};
        s.players[0].facing = {kFixedScale, 0};
        s.players[0].effect_kind = EffectKind::PulseGate;
        s.players[0].effect_ticks = 120;
        s.players[0].effect_position = {kFieldWidth / 4 + 4 * kFixedScale,
                                        kFieldHalfY};
        s.core.position = {s.players[0].effect_position.x - kGateRadius -
                               kCoreRadius - 50,
                           kFieldHalfY};
        s.core.velocity = {300, 0};
        sim.set_state(s);
        StepEvents events;
        for (int i = 0; i < 12; ++i) {
            events = StepEvents{};
            sim.step({}, &events);
            if (events.count > 0) break;
        }
        check(events.count > 0, "gate bounce emits event");
        check(events.events[0].type == SimulationEventType::CoreBounce,
              "gate bounce emits CoreBounce");
        check(events.events[0].actor == 0, "gate bounce has correct actor");
        check(events.events[0].effect_kind == EffectKind::PulseGate,
              "gate bounce has correct effect kind");
    }

    // No bounce event on separating core.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Live;
        s.kickoff_remaining = 0;
        s.players[0].position = {kFieldWidth / 4, kFieldHalfY};
        s.players[0].velocity = {0, 0};
        s.core.position = {s.players[0].position.x + kPlayerRadius +
                               kCoreRadius - 100,
                           s.players[0].position.y};
        s.core.velocity = {100, 0};  // Moving away, not entering
        sim.set_state(s);
        StepEvents events;
        sim.step({}, &events);
        check(events.count == 0, "separating core emits no bounce event");
    }
}

void test_goal_event() {
    Simulation sim;
    GameState s = sim.state();
    s.phase = Phase::Live;
    s.kickoff_remaining = 0;
    s.core.position = {kFieldWidth - 250, kFieldHalfY};
    s.core.velocity = {300, 0};
    sim.set_state(s);
    StepEvents events;
    StepResult r = sim.step({}, &events);
    check(r.goal_scored, "goal is scored");
    check(events.count == 1, "goal emits exactly one event");
    check(events.events[0].type == SimulationEventType::GoalScored,
          "goal emits GoalScored");
    check(events.events[0].actor == 0, "goal event has correct scoring team");
}

void test_events_non_authoritative() {
    // State and hash must be identical with and without events.
    Simulation sim1({Character::Kite, Character::Bastion});
    Simulation sim2({Character::Kite, Character::Bastion});

    for (std::int32_t i = 0; i < 1000; ++i) {
        std::array<FrameInput, 2> in = {scripted(static_cast<std::uint32_t>(i), 0),
                                        scripted(static_cast<std::uint32_t>(i), 1)};
        StepEvents events;
        sim1.step(in, &events);
        sim2.step(in, nullptr);
        check(sim1.state() == sim2.state(),
              "state is identical with and without events");
        check(sim1.state_hash() == sim2.state_hash(),
              "hash is identical with and without events");
    }

    // Optional StepEvents buffer still receives tick/decision boundary on Kickoff.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::Kickoff;
        s.kickoff_remaining = 5;
        sim.set_state(s);
        StepEvents events;
        sim.step({}, &events);
        check(events.tick == 1, "Kickoff step populates tick");
        check(!events.decision_boundary, "Kickoff step populates decision_boundary");
    }

    // Optional StepEvents buffer still receives tick/decision boundary on MatchOver.
    {
        Simulation sim;
        GameState s = sim.state();
        s.phase = Phase::MatchOver;
        sim.set_state(s);
        StepEvents events;
        sim.step({}, &events);
        check(events.tick == 1, "MatchOver step populates tick");
        check(!events.decision_boundary, "MatchOver step populates decision_boundary");
    }
}

}  // namespace tests
}  // namespace pulse

int main() {
    using namespace pulse::tests;
    test_fixed_math();
    test_determinism();
    test_state_restore();
    test_mirror_identity();
    test_goal_scores();
    test_core_contact_and_substeps();
    test_wall_bounce();
    test_wall_bounce_no_false_event();
    test_no_tunneling();
    test_strike_and_dash();
    test_abilities();
    test_action_space();
    test_replay();
    test_court_geometry();
    test_decision_boundary();
    test_action_events();
    test_strike_hit_event();
    test_core_bounce_events();
    test_goal_event();
    test_events_non_authoritative();
    report();
    return 0;
}
