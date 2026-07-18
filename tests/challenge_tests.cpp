#include "challenge_session.hpp"
#include "challenge_cli.hpp"
#include "challenge_smoke.hpp"
#include "pulse_replay.hpp"
#include "pulse_sim.hpp"
#include "scripted_policy.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace pulse {
namespace test {

void test_character_name() {
    // character_name is defined in pulse_sim.hpp
    assert(std::string(character_name(Character::Kite)) == "kite");
    assert(std::string(character_name(Character::Vale)) == "vale");
    assert(std::string(character_name(Character::Bastion)) == "bastion");
    std::cout << "test_character_name passed\n";
}

void test_schedule_generation() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test_checkpoint";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc123";
    policy.manifest_sha = "def456";
    policy.certification_sha = "ghi789";

    std::filesystem::path test_dir = "test_challenge_output";
    std::filesystem::remove_all(test_dir);

    ChallengeSession session(policy, test_dir, 20);
    assert(session.valid());
    assert(session.error().empty());

    // Test exact 20 schedule
    for (std::int32_t i = 0; i < 20; ++i) {
        GameSchedule gs = session.schedule_game(i);
        assert(gs.human_seat == i % 2);
        assert(gs.bot_seat == 1 - gs.human_seat);
    }

    // Test strict alternating seats/10 each
    std::int32_t seat_0_count = 0;
    std::int32_t seat_1_count = 0;
    for (std::int32_t i = 0; i < 20; ++i) {
        GameSchedule gs = session.schedule_game(i);
        if (gs.human_seat == 0) seat_0_count++;
        else seat_1_count++;
    }
    assert(seat_0_count == 10);
    assert(seat_1_count == 10);

    // Test every ordered pair once per seat in first 18
    std::vector<std::pair<Character, Character>> pairs_0;
    std::vector<std::pair<Character, Character>> pairs_1;
    for (std::int32_t i = 0; i < 18; ++i) {
        GameSchedule gs = session.schedule_game(i);
        if (gs.human_seat == 0) {
            pairs_0.push_back({gs.characters[0], gs.characters[1]});
        } else {
            pairs_1.push_back({gs.characters[1], gs.characters[0]});
        }
    }
    // Should have all 9 ordered pairs
    assert(pairs_0.size() == 9);
    assert(pairs_1.size() == 9);

    std::filesystem::remove_all(test_dir);
    std::cout << "test_schedule_generation passed\n";
}

void test_game_evidence_recording() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test_checkpoint";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc123";
    policy.manifest_sha = "def456";
    policy.certification_sha = "ghi789";

    std::filesystem::path test_dir = "test_challenge_output";
    std::filesystem::remove_all(test_dir);

    ChallengeSession session(policy, test_dir, 20);
    assert(session.valid());

    // Test game start sequence
    assert(session.start_game(0));
    assert(!session.start_game(0)); // duplicate
    assert(!session.start_game(2)); // skip

    GameEvidence ev;
    ev.game_index = 0;
    ev.characters = {Character::Kite, Character::Vale};
    ev.human_seat = 0;
    ev.bot_seat = 1;
    ev.winner = 0;
    ev.human_score = 1;
    ev.bot_score = 0;
    ev.final_state_hash = "hash123";
    ev.replay_path = "replay.json";

    session.record_game_result(ev);

    SessionSummary s = session.summary();
    assert(s.completed_games == 1);
    assert(s.human_wins == 1);
    assert(s.bot_wins == 0);
    assert(s.human_seat_0_count == 1);
    assert(s.human_seat_1_count == 0);

    std::filesystem::remove_all(test_dir);
    std::cout << "test_game_evidence_recording passed\n";
}

void test_gate_conditions() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test_checkpoint";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc123";
    policy.manifest_sha = "def456";
    policy.certification_sha = "ghi789";

    std::filesystem::path test_dir = "test_challenge_output";
    std::filesystem::remove_all(test_dir);

    ChallengeSession session(policy, test_dir, 20);
    assert(session.valid());

    // Test gate 13 fail
    for (std::int32_t i = 0; i < 20; ++i) {
        session.start_game(i);
        GameEvidence ev;
        ev.game_index = i;
        ev.characters = {Character::Kite, Character::Vale};
        ev.human_seat = i % 2;
        ev.bot_seat = 1 - ev.human_seat;
        ev.winner = ev.human_seat; // human wins all
        ev.human_score = 1;
        ev.bot_score = 0;
        ev.final_state_hash = "hash123";
        ev.replay_path = "replay.json";
        session.record_game_result(ev);
    }

    SessionSummary s = session.summary();
    assert(s.human_wins == 20);
    assert(s.bot_wins == 0);
    assert(!s.gate_passed); // bot needs 14 wins

    std::filesystem::remove_all(test_dir);

    // Test gate 14 pass
    ChallengeSession session2(policy, test_dir, 20);
    assert(session2.valid());

    for (std::int32_t i = 0; i < 20; ++i) {
        session2.start_game(i);
        GameEvidence ev;
        ev.game_index = i;
        ev.characters = {Character::Kite, Character::Vale};
        ev.human_seat = i % 2;
        ev.bot_seat = 1 - ev.human_seat;
        ev.winner = ev.bot_seat; // bot wins all
        ev.human_score = 0;
        ev.bot_score = 1;
        ev.final_state_hash = "hash123";
        ev.replay_path = "replay.json";
        session2.record_game_result(ev);
    }

    SessionSummary s2 = session2.summary();
    assert(s2.human_wins == 0);
    assert(s2.bot_wins == 20);
    assert(s2.gate_passed); // bot has 14+ wins

    std::filesystem::remove_all(test_dir);
    std::cout << "test_gate_conditions passed\n";
}

void test_file_integrity() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test_checkpoint";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc123";
    policy.manifest_sha = "def456";
    policy.certification_sha = "ghi789";

    std::filesystem::path test_dir = "test_challenge_output";
    std::filesystem::remove_all(test_dir);

    ChallengeSession session(policy, test_dir, 20);
    assert(session.valid());

    // Check session.json exists
    assert(std::filesystem::exists(test_dir / "session.json"));

    // Record a game
    session.start_game(0);
    GameEvidence ev;
    ev.game_index = 0;
    ev.characters = {Character::Kite, Character::Vale};
    ev.human_seat = 0;
    ev.bot_seat = 1;
    ev.winner = 0;
    ev.human_score = 1;
    ev.bot_score = 0;
    ev.final_state_hash = "hash123";
    ev.replay_path = "replay.json";
    session.record_game_result(ev);

    // Check games.jsonl exists
    assert(std::filesystem::exists(test_dir / "games.jsonl"));

    // Write summary
    session.write_summary(ChallengeStatus::Complete);
    assert(std::filesystem::exists(test_dir / "summary.json"));

    std::filesystem::remove_all(test_dir);
    std::cout << "test_file_integrity passed\n";
}

void test_new_dir_no_overwrite() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test_checkpoint";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc123";
    policy.manifest_sha = "def456";
    policy.certification_sha = "ghi789";

    std::filesystem::path test_dir = "test_challenge_output";
    std::filesystem::remove_all(test_dir);

    // Create new directory
    ChallengeSession session(policy, test_dir, 20);
    assert(session.valid());

    // Try to create session in same directory (should fail)
    ChallengeSession session2(policy, test_dir, 20);
    assert(!session2.valid());
    assert(!session2.error().empty());

    std::filesystem::remove_all(test_dir);
    std::cout << "test_new_dir_no_overwrite passed\n";
}

void test_incomplete_seat_mismatch() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test_checkpoint";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc123";
    policy.manifest_sha = "def456";
    policy.certification_sha = "ghi789";

    std::filesystem::path test_dir = "test_challenge_output";
    std::filesystem::remove_all(test_dir);

    ChallengeSession session(policy, test_dir, 20);
    assert(session.valid());

    session.start_game(0);
    GameEvidence ev;
    ev.game_index = 0;
    ev.characters = {Character::Kite, Character::Vale};
    ev.human_seat = 0;
    ev.bot_seat = 1;
    ev.winner = 0;
    ev.human_score = 1;
    ev.bot_score = 0;
    ev.final_state_hash = "hash123";
    ev.replay_path = "replay.json";
    session.record_game_result(ev);

    // Try to record with wrong seat
    ev.human_seat = 1;
    session.record_game_result(ev);

    SessionSummary s = session.summary();
    assert(s.completed_games == 1); // Should not record mismatched seat

    std::filesystem::remove_all(test_dir);
    std::cout << "test_incomplete_seat_mismatch passed\n";
}

void test_cli_viewer_args() {
    const wchar_t* argv[] = {L"program", L"--challenge", L"--policy-bundle", L"bundle.pzb", L"--policy-manifest", L"manifest.json", L"--challenge-games", L"20", L"--challenge-log-dir", L"logs"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    ChallengeViewerArgs args = parse_challenge_viewer_args(argc, argv);
    assert(args.challenge);
    assert(args.bundle_path == "bundle.pzb");
    assert(args.manifest_path == "manifest.json");
    assert(args.games == 20);
    assert(args.log_dir == "logs");
    assert(args.error.empty());

    std::cout << "test_cli_viewer_args passed\n";
}

void test_cli_headless_args() {
    const char* argv[] = {"program", "--challenge-smoke", "--policy-bundle", "bundle.pzb", "--policy-manifest", "manifest.json", "--games", "6", "--output-dir", "output"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    ChallengeHeadlessArgs args = parse_challenge_headless_args(argc, argv);
    assert(args.smoke);
    assert(args.bundle_path == "bundle.pzb");
    assert(args.manifest_path == "manifest.json");
    assert(args.games == 6);
    assert(args.output_dir == "output");
    assert(args.error.empty());

    std::cout << "test_cli_headless_args passed\n";
}

void test_cli_validation() {
    // Test missing required args
    const wchar_t* argv1[] = {L"program", L"--challenge"};
    ChallengeViewerArgs args1 = parse_challenge_viewer_args(1, argv1);
    assert(!args1.error.empty());

    // Test wrong games count
    const wchar_t* argv2[] = {L"program", L"--challenge", L"--policy-bundle", L"bundle.pzb", L"--policy-manifest", L"manifest.json", L"--challenge-games", L"10", L"--challenge-log-dir", L"logs"};
    ChallengeViewerArgs args2 = parse_challenge_viewer_args(9, argv2);
    assert(!args2.error.empty());

    // Test duplicate args
    const wchar_t* argv3[] = {L"program", L"--challenge", L"--challenge", L"--policy-bundle", L"bundle.pzb", L"--policy-manifest", L"manifest.json", L"--challenge-games", L"20", L"--challenge-log-dir", L"logs"};
    ChallengeViewerArgs args3 = parse_challenge_viewer_args(10, argv3);
    assert(!args3.error.empty());

    // Test unknown flags
    const wchar_t* argv4[] = {L"program", L"--challenge", L"--policy-bundle", L"bundle.pzb", L"--policy-manifest", L"manifest.json", L"--challenge-games", L"20", L"--challenge-log-dir", L"logs", L"--unknown-flag"};
    ChallengeViewerArgs args4 = parse_challenge_viewer_args(10, argv4);
    assert(!args4.error.empty());

    std::cout << "test_cli_validation passed\n";
}

void test_cli_headless_validation() {
    // Test missing required args
    const char* argv1[] = {"program", "--challenge-smoke"};
    ChallengeHeadlessArgs args1 = parse_challenge_headless_args(2, argv1);
    assert(!args1.error.empty());

    // Test wrong games count
    const char* argv2[] = {"program", "--challenge-smoke", "--policy-bundle", "bundle.pzb", "--policy-manifest", "manifest.json", "--games", "5", "--output-dir", "output"};
    ChallengeHeadlessArgs args2 = parse_challenge_headless_args(9, argv2);
    assert(!args2.error.empty());

    // Test duplicate args
    const char* argv3[] = {"program", "--challenge-smoke", "--challenge-smoke", "--policy-bundle", "bundle.pzb", "--policy-manifest", "manifest.json", "--games", "6", "--output-dir", "output"};
    ChallengeHeadlessArgs args3 = parse_challenge_headless_args(10, argv3);
    assert(!args3.error.empty());

    // Test unknown flags
    const char* argv4[] = {"program", "--challenge-smoke", "--policy-bundle", "bundle.pzb", "--policy-manifest", "manifest.json", "--games", "6", "--output-dir", "output", "--unknown"};
    ChallengeHeadlessArgs args4 = parse_challenge_headless_args(10, argv4);
    assert(!args4.error.empty());

    std::cout << "test_cli_headless_validation passed\n";
}

void test_policy_controller_reset() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc";
    policy.manifest_sha = "def";
    policy.certification_sha = "ghi";

    ScriptedPolicyController controller(policy, 0, Character::Kite, Character::Vale);

    // Test reset per game
    GameState state;
    state.tick = 0;
    controller.reset(state);

    FrameInput in1 = controller.decide(state, 0, 0);
    assert(in1.buttons == ButtonStrike); // First tick strikes

    // Reset for new game
    state.tick = 1000;
    controller.reset(state);

    FrameInput in2 = controller.decide(state, 1000, 0);
    assert(in2.buttons == ButtonStrike); // Should strike again after reset

    std::cout << "test_policy_controller_reset passed\n";
}

void test_policy_controller_decisions() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc";
    policy.manifest_sha = "def";
    policy.certification_sha = "ghi";

    ScriptedPolicyController controller(policy, 0, Character::Kite, Character::Vale);

    GameState state;
    state.tick = 0;
    controller.reset(state);

    // Test decisions at 0/12/24 ticks
    FrameInput in0 = controller.decide(state, 0, 0);
    assert(in0.buttons == ButtonStrike);

    state.tick = 12;
    FrameInput in12 = controller.decide(state, 12, 0);
    assert(in12.buttons == 0); // Not a strike tick

    state.tick = 24;
    FrameInput in24 = controller.decide(state, 24, 0);
    assert(in24.buttons == 0); // Not a strike tick

    state.tick = 120;
    FrameInput in120 = controller.decide(state, 120, 0);
    assert(in120.buttons == ButtonStrike); // Strike every 120 ticks

    std::cout << "test_policy_controller_decisions passed\n";
}

void test_policy_controller_held_axes() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc";
    policy.manifest_sha = "def";
    policy.certification_sha = "ghi";

    ScriptedPolicyController controller(policy, 0, Character::Kite, Character::Vale);

    GameState state;
    state.tick = 0;
    state.core.position.x = 100;
    controller.reset(state);

    // Test held axes (movement toward ball)
    FrameInput in1 = controller.decide(state, 1, 0);
    assert(in1.move_x == 1); // Moving toward positive ball
    assert(in1.aim_x == 1);

    state.core.position.x = -100;
    FrameInput in2 = controller.decide(state, 2, 0);
    assert(in2.move_x == -1); // Moving toward negative ball
    assert(in2.aim_x == -1);

    std::cout << "test_policy_controller_held_axes passed\n";
}

void test_policy_controller_one_tick_button() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc";
    policy.manifest_sha = "def";
    policy.certification_sha = "ghi";

    ScriptedPolicyController controller(policy, 0, Character::Kite, Character::Vale);

    GameState state;
    state.tick = 0;
    controller.reset(state);

    // Test one-tick button press
    FrameInput in0 = controller.decide(state, 0, 0);
    assert(in0.buttons == ButtonStrike);

    FrameInput in1 = controller.decide(state, 1, 0);
    assert(in1.buttons == 0); // Button released next tick

    std::cout << "test_policy_controller_one_tick_button passed\n";
}

void test_policy_controller_no_stale_button() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc";
    policy.manifest_sha = "def";
    policy.certification_sha = "ghi";

    ScriptedPolicyController controller(policy, 0, Character::Kite, Character::Vale);

    GameState state;
    state.tick = 0;
    controller.reset(state);

    // Get decision at tick 0
    FrameInput in0 = controller.decide(state, 0, 0);
    assert(in0.buttons == ButtonStrike);

    // Simulate game transition
    state.tick = 1000;
    controller.reset(state);

    // Decision at new game should not have stale button
    FrameInput in1000 = controller.decide(state, 1000, 0);
    assert(in1000.buttons == ButtonStrike); // Fresh strike, not stale

    std::cout << "test_policy_controller_no_stale_button passed\n";
}

void test_policy_controller_both_seats() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc";
    policy.manifest_sha = "def";
    policy.certification_sha = "ghi";

    // Test seat 0
    ScriptedPolicyController controller0(policy, 0, Character::Kite, Character::Vale);
    GameState state0;
    state0.tick = 0;
    controller0.reset(state0);
    FrameInput in0 = controller0.decide(state0, 0, 0);
    assert(in0.buttons == ButtonStrike);

    // Test seat 1
    ScriptedPolicyController controller1(policy, 1, Character::Vale, Character::Kite);
    GameState state1;
    state1.tick = 0;
    controller1.reset(state1);
    FrameInput in1 = controller1.decide(state1, 0, 1);
    assert(in1.buttons == ButtonStrike);

    std::cout << "test_policy_controller_both_seats passed\n";
}

void test_session_reset_discard() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc";
    policy.manifest_sha = "def";
    policy.certification_sha = "ghi";

    std::filesystem::path test_dir = "test_challenge_output";
    std::filesystem::remove_all(test_dir);

    ChallengeSession session(policy, test_dir, 20);
    assert(session.valid());

    // Test can_reset before any games
    assert(session.can_reset());
    assert(!session.can_discard());

    // Start a game
    session.start_game(0);
    GameEvidence ev;
    ev.game_index = 0;
    ev.characters = {Character::Kite, Character::Vale};
    ev.human_seat = 0;
    ev.bot_seat = 1;
    ev.winner = 0;
    ev.human_score = 1;
    ev.bot_score = 0;
    ev.final_state_hash = "hash123";
    ev.replay_path = "replay.json";
    session.record_game_result(ev);

    // Now can discard
    assert(session.can_reset());
    assert(session.can_discard());

    // Test reset
    session.reset_session();
    assert(session.valid());
    assert(session.summary().completed_games == 0);

    // Start another game
    session.start_game(0);
    session.record_game_result(ev);

    // Test discard
    session.discard_session();
    assert(!session.valid());
    assert(!session.can_reset());
    assert(!session.can_discard());

    std::filesystem::remove_all(test_dir);
    std::cout << "test_session_reset_discard passed\n";
}

void test_session_partial_summary() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc";
    policy.manifest_sha = "def";
    policy.certification_sha = "ghi";

    std::filesystem::path test_dir = "test_challenge_output";
    std::filesystem::remove_all(test_dir);

    ChallengeSession session(policy, test_dir, 20);
    assert(session.valid());

    // Complete some games
    for (std::int32_t i = 0; i < 5; ++i) {
        session.start_game(i);
        GameEvidence ev;
        ev.game_index = i;
        ev.characters = {Character::Kite, Character::Vale};
        ev.human_seat = i % 2;
        ev.bot_seat = 1 - ev.human_seat;
        ev.winner = ev.human_seat;
        ev.human_score = 1;
        ev.bot_score = 0;
        ev.final_state_hash = "hash123";
        ev.replay_path = "replay.json";
        session.record_game_result(ev);
    }

    // Write partial summary
    session.write_summary(ChallengeStatus::Partial);

    // Check summary file exists and has partial status
    assert(std::filesystem::exists(test_dir / "summary.json"));

    std::filesystem::remove_all(test_dir);
    std::cout << "test_session_partial_summary passed\n";
}

// NOTE: assert() is compiled out under NDEBUG (Release builds). Any call
// with a side effect this file depends on must happen in a plain statement
// first, with the boolean result stashed in a variable that assert() then
// checks — never call a side-effecting function directly inside assert().

void test_smoke_game_records_replay() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc";
    policy.manifest_sha = "def";
    policy.certification_sha = "ghi";

    std::filesystem::path test_dir = "test_challenge_output";
    std::filesystem::remove_all(test_dir);
    std::filesystem::create_directories(test_dir);

    GameSchedule gs;
    gs.characters = {Character::Kite, Character::Vale};
    gs.human_seat = 0;
    gs.bot_seat = 1;

    GameEvidence ev;
    std::string error;
    bool smoke_ok = run_smoke_game(policy, gs, 0, test_dir, ev, error);
    assert(smoke_ok);
    (void)smoke_ok;
    assert(error.empty());

    std::filesystem::path replay_file = test_dir / "game_000.pulse";
    assert(std::filesystem::exists(replay_file));
    assert(std::filesystem::file_size(replay_file) > 0);
    assert(ev.replay_path == "game_000.pulse");

    Replay loaded;
    std::string load_error;
    bool load_ok = load_replay(replay_file.string(), loaded, load_error);
    assert(load_ok);
    (void)load_ok;
    ReplayVerification v = verify_replay(loaded);
    assert(v.ok);
    assert(!loaded.ticks.empty());
    assert(loaded.ticks.back().expected_hash == std::stoull(ev.final_state_hash));

    std::filesystem::remove_all(test_dir);
    std::cout << "test_smoke_game_records_replay passed\n";
}

void test_smoke_replay_path_in_games_jsonl() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc";
    policy.manifest_sha = "def";
    policy.certification_sha = "ghi";

    std::filesystem::path test_dir = "test_challenge_output";
    std::filesystem::remove_all(test_dir);

    ChallengeSession session(policy, test_dir, 20);
    assert(session.valid());

    GameSchedule gs = session.schedule_game(0);
    GameEvidence ev;
    std::string error;
    bool smoke_ok = run_smoke_game(policy, gs, 0, test_dir, ev, error);
    assert(smoke_ok);
    (void)smoke_ok;

    bool started = session.start_game(0);
    assert(started);
    (void)started;
    session.record_game_result(ev);

    std::ifstream in(test_dir / "games.jsonl");
    assert(in.good());
    std::string line;
    bool found = false;
    while (std::getline(in, line)) {
        if (line.find("\"replay_path\":\"game_000.pulse\"") != std::string::npos) {
            found = true;
        }
    }
    in.close();
    assert(found);

    std::filesystem::remove_all(test_dir);
    std::cout << "test_smoke_replay_path_in_games_jsonl passed\n";
}

namespace {

void patch_bytes(const std::filesystem::path& path, std::size_t offset,
                 std::uint32_t value) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    in.close();
    assert(offset + sizeof(value) <= data.size());
    std::memcpy(data.data() + offset, &value, sizeof(value));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

}  // namespace

void test_replay_version_rejection() {
    std::filesystem::path test_dir = "test_challenge_output";
    std::filesystem::remove_all(test_dir);
    std::filesystem::create_directories(test_dir);

    Replay replay;
    replay.characters = {Character::Kite, Character::Vale};
    RecordedTick rt;
    Simulation sim(replay.characters, replay.config);
    StepResult step_result = sim.step(rt.inputs);
    (void)step_result;
    rt.expected_hash = sim.state_hash();
    replay.ticks.push_back(rt);

    std::filesystem::path replay_path = test_dir / "version_test.pulse";
    std::string error;
    bool save_ok = save_replay(replay, replay_path.string(), error);
    assert(save_ok);
    (void)save_ok;

    // Patch format_version at offset 8.
    patch_bytes(replay_path, 8, 9999u);
    Replay loaded;
    std::string load_error;
    bool load_ok = load_replay(replay_path.string(), loaded, load_error);
    assert(!load_ok);
    (void)load_ok;

    // Re-save clean, patch ruleset_version at offset 12.
    save_ok = save_replay(replay, replay_path.string(), error);
    assert(save_ok);
    patch_bytes(replay_path, 12, 9999u);
    Replay loaded2;
    std::string load_error2;
    bool load_ok2 = load_replay(replay_path.string(), loaded2, load_error2);
    assert(!load_ok2);
    (void)load_ok2;

    // Truncated file case.
    save_ok = save_replay(replay, replay_path.string(), error);
    assert(save_ok);
    {
        std::ifstream in(replay_path, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        in.close();
        assert(data.size() > 4);
        data.resize(data.size() - 4);
        std::ofstream out(replay_path, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    Replay loaded3;
    std::string load_error3;
    bool load_ok3 = load_replay(replay_path.string(), loaded3, load_error3);
    assert(!load_ok3);
    (void)load_ok3;

    std::filesystem::remove_all(test_dir);
    std::cout << "test_replay_version_rejection passed\n";
}

void test_smoke_determinism() {
    PolicyMetadata policy;
    policy.checkpoint_id = "test";
    policy.model_version = "v1.0";
    policy.bundle_sha = "abc";
    policy.manifest_sha = "def";
    policy.certification_sha = "ghi";

    std::filesystem::path dir_a = "test_challenge_output_a";
    std::filesystem::path dir_b = "test_challenge_output_b";
    std::filesystem::remove_all(dir_a);
    std::filesystem::remove_all(dir_b);
    std::filesystem::create_directories(dir_a);
    std::filesystem::create_directories(dir_b);

    GameSchedule gs;
    gs.characters = {Character::Kite, Character::Vale};
    gs.human_seat = 0;
    gs.bot_seat = 1;

    GameEvidence ev_a, ev_b;
    std::string error_a, error_b;
    bool ok_a = run_smoke_game(policy, gs, 0, dir_a, ev_a, error_a);
    assert(ok_a);
    (void)ok_a;
    bool ok_b = run_smoke_game(policy, gs, 0, dir_b, ev_b, error_b);
    assert(ok_b);
    (void)ok_b;

    assert(ev_a.final_state_hash == ev_b.final_state_hash);

    std::filesystem::remove_all(dir_a);
    std::filesystem::remove_all(dir_b);
    std::cout << "test_smoke_determinism passed\n";
}

void run_all_tests() {
    std::cout << "Running challenge tests...\n";
    test_character_name();
    test_schedule_generation();
    test_game_evidence_recording();
    test_gate_conditions();
    test_file_integrity();
    test_new_dir_no_overwrite();
    test_incomplete_seat_mismatch();
    test_cli_viewer_args();
    test_cli_headless_args();
    test_cli_validation();
    test_cli_headless_validation();
    test_policy_controller_reset();
    test_policy_controller_decisions();
    test_policy_controller_held_axes();
    test_policy_controller_one_tick_button();
    test_policy_controller_no_stale_button();
    test_policy_controller_both_seats();
    test_session_reset_discard();
    test_session_partial_summary();
    std::cout.flush();
    test_smoke_game_records_replay();
    std::cout.flush();
    test_smoke_replay_path_in_games_jsonl();
    std::cout.flush();
    test_replay_version_rejection();
    std::cout.flush();
    test_smoke_determinism();
    std::cout.flush();
    std::cout << "All challenge tests passed!\n";
}

} // namespace test
} // namespace pulse

int main() {
    pulse::test::run_all_tests();
    return 0;
}
