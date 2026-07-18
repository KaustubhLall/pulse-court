#include "pulse_replay.hpp"
#include "pulse_sim.hpp"
#include "challenge_session.hpp"
#include "challenge_cli.hpp"
#include "challenge_smoke.hpp"
#include "pulse_policy.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace pulse {

namespace {

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --ticks N           simulate N ticks (default 12000)\n"
              << "  --left kite|vale|bastion   left player character\n"
              << "  --right kite|vale|bastion  right player character\n"
              << "  --record PATH       write a replay file\n"
              << "  --replay PATH       load and verify a replay file\n"
              << "  --verify            double-run consistency check\n"
              << "  --benchmark N       run N ticks and report throughput\n"
              << "  --hash-interval N   print state hash every N ticks\n";
}

}  // namespace

}  // namespace pulse

int main(int argc, char** argv) {
    using namespace pulse;

    ChallengeHeadlessArgs smoke_args =
        parse_challenge_headless_args(argc, const_cast<const char**>(argv));
    if (smoke_args.smoke) {
        if (!smoke_args.error.empty()) {
            std::cerr << "Challenge smoke error: " << smoke_args.error << "\n";
            print_usage(argv[0]);
            return 1;
        }

        // Create a synthetic policy for smoke testing
        Policy policy;
        policy.checkpoint_id = "smoke_test_checkpoint";
        policy.model_version = "v1.0";
        policy.bundle_sha = "synthetic_bundle_sha";
        policy.manifest_sha = "synthetic_manifest_sha";
        policy.certification_sha = "synthetic_cert_sha";

        ChallengeSession session(policy, smoke_args.output_dir,
                                 smoke_args.games);
        if (!session.valid()) {
            std::cerr << "Challenge session failed: " << session.error()
                      << "\n";
            return 1;
        }

        std::cout << "Running challenge smoke mode: " << smoke_args.games
                  << " games\n";

        for (std::int32_t game_idx = 0; game_idx < smoke_args.games;
             ++game_idx) {
            GameSchedule gs = session.schedule_game(game_idx);

            session.start_game(game_idx);

            GameEvidence ev;
            std::string error;
            if (!run_smoke_game(policy, gs, game_idx, smoke_args.output_dir,
                                ev, error)) {
                std::cerr << "Replay record failed: " << error << "\n";
                return 1;
            }

            session.record_game_result(ev);

            std::cout << "Game " << (game_idx + 1) << ": human "
                      << static_cast<int>(ev.human_score) << " - bot "
                      << static_cast<int>(ev.bot_score)
                      << " winner=" << static_cast<int>(ev.winner)
                      << " replay=" << ev.replay_path << "\n";
        }

        session.write_summary(ChallengeStatus::Complete);
        SessionSummary s = session.summary();
        std::cout << "Smoke complete. Bot wins: " << s.bot_wins
                  << " Human wins: " << s.human_wins << "\n";
        return 0;
    }

    std::array<Character, 2> characters = {Character::Kite, Character::Vale};
    std::int32_t ticks = 12000;
    std::string record_path;
    std::string replay_path;
    bool verify = false;
    std::int64_t benchmark = -1;
    std::int32_t hash_interval = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](std::string& out) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << arg << "\n";
                return false;
            }
            out = argv[++i];
            return true;
        };
        if (arg == "--left") {
            std::string v;
            if (!next(v) || !parse_character(v.c_str(), characters[0])) {
                std::cerr << "invalid --left value\n";
                return 1;
            }
        } else if (arg == "--right") {
            std::string v;
            if (!next(v) || !parse_character(v.c_str(), characters[1])) {
                std::cerr << "invalid --right value\n";
                return 1;
            }
        } else if (arg == "--ticks") {
            std::string v;
            if (!next(v)) return 1;
            ticks = std::stoi(v);
        } else if (arg == "--record") {
            if (!next(record_path)) return 1;
        } else if (arg == "--replay") {
            if (!next(replay_path)) return 1;
        } else if (arg == "--verify") {
            verify = true;
        } else if (arg == "--benchmark") {
            std::string v;
            if (!next(v)) return 1;
            benchmark = std::stoll(v);
        } else if (arg == "--hash-interval") {
            std::string v;
            if (!next(v)) return 1;
            hash_interval = std::stoi(v);
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 1;
        }
    }

    if (!replay_path.empty()) {
        Replay replay;
        std::string error;
        if (!load_replay(replay_path, replay, error)) {
            std::cerr << "replay load failed: " << error << "\n";
            return 1;
        }
        ReplayVerification v = verify_replay(replay);
        if (!v.ok) {
            std::cerr << "replay verification failed at tick " << v.failing_tick
                      << "\n  expected: " << v.expected_hash
                      << "\n  actual:   " << v.actual_hash << "\n";
            return 1;
        }
        std::cout << "replay verified: " << replay.ticks.size()
                  << " ticks OK\n";
        return 0;
    }

    Simulation sim(characters);

    if (benchmark > 0) {
        auto start = std::chrono::steady_clock::now();
        for (std::int64_t i = 0; i < benchmark; ++i) {
            std::array<FrameInput, 2> in = {
                scripted_input(static_cast<std::uint32_t>(i), 0,
                               characters[0]),
                scripted_input(static_cast<std::uint32_t>(i), 1,
                               characters[1])};
            sim.step(in);
        }
        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                        start)
                      .count();
        double tps = (ms > 0)
                         ? (benchmark * 1000.0 / static_cast<double>(ms))
                         : 0.0;
        std::cout << "benchmark: " << benchmark << " ticks in " << ms
                  << " ms\n";
        std::cout << "throughput: " << tps << " ticks/sec\n";
        std::cout << "final hash: " << sim.state_hash() << "\n";
        return 0;
    }

    if (ticks <= 0) ticks = 1;
    std::vector<std::uint64_t> hashes;
    std::vector<std::array<FrameInput, 2>> tape;
    std::vector<std::uint64_t> tick_numbers;
    Replay replay;
    replay.characters = characters;
    replay.config = sim.config();

    for (std::int32_t i = 0; i < ticks; ++i) {
        std::uint32_t t = static_cast<std::uint32_t>(i);
        std::array<FrameInput, 2> in = {scripted_input(t, 0, characters[0]),
                                        scripted_input(t, 1, characters[1])};
        sim.step(in);
        std::uint64_t h = sim.state_hash();
        if (hash_interval > 0 && ((i + 1) % hash_interval == 0)) {
            std::cout << "tick " << (i + 1) << " hash " << h << "\n";
        }
        hashes.push_back(h);
        tape.push_back(in);
        tick_numbers.push_back(sim.state().tick);

        RecordedTick rt;
        rt.inputs = in;
        rt.expected_hash = h;
        replay.ticks.push_back(rt);
    }

    std::cout << "simulated " << ticks << " ticks\n";
    std::cout << "final score: " << sim.state().score[0] << " - "
              << sim.state().score[1] << "\n";
    std::cout << "final phase: "
              << static_cast<int>(sim.state().phase) << "\n";
    std::cout << "final hash:  " << sim.state_hash() << "\n";

    if (!record_path.empty()) {
        std::string error;
        if (!save_replay(replay, record_path, error)) {
            std::cerr << "failed to record: " << error << "\n";
            return 1;
        }
        std::cout << "recorded replay to " << record_path << "\n";
    }

    if (verify) {
        Simulation sim2(characters);
        for (std::int32_t i = 0; i < ticks; ++i) {
            sim2.step(tape[i]);
            std::uint64_t h = sim2.state_hash();
            if (h != hashes[i]) {
                std::cerr << "verify failed at tick " << tick_numbers[i]
                          << "\n  expected: " << hashes[i]
                          << "\n  actual:   " << h << "\n";
                return 1;
            }
        }
        std::cout << "verify OK: deterministic replay reproduced " << ticks
                  << " hashes\n";

        if (!record_path.empty()) {
            Replay loaded;
            std::string error;
            if (!load_replay(record_path, loaded, error)) {
                std::cerr << "verify load failed: " << error << "\n";
                return 1;
            }
            ReplayVerification v = verify_replay(loaded);
            if (!v.ok) {
                std::cerr
                    << "recorded replay verification failed at tick "
                    << v.failing_tick << "\n  expected: " << v.expected_hash
                    << "\n  actual:   " << v.actual_hash << "\n";
                return 1;
            }
            std::cout << "recorded replay file verified OK\n";
        }
    }

    return 0;
}
