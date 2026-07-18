#include "test_no_dialogs.hpp"
#include "challenge_protocol.hpp"
#include "pulse_sim.hpp"

#include <iostream>
#include <string>
#include <filesystem>
#include <ctime>
#include <vector>
#include <fstream>
#include <sstream>
#include <cctype>

namespace fs = std::filesystem;

// Always-on test requirement mechanism (works in Release builds)
#define REQUIRE(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "REQUIRE FAILED: " << message << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while(0)

#define REQUIRE_EQ(a, b, message) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "REQUIRE FAILED: " << message << " (expected " << (b) << ", got " << (a) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while(0)

// Tiny JSON parser for test validation
namespace {
    std::string json_trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\n\r");
        return s.substr(start, end - start + 1);
    }
    
    bool json_parse_string(const std::string& json, const std::string& key, std::string& value) {
        std::string search_key = "\"" + key + "\"";
        size_t pos = json.find(search_key);
        if (pos == std::string::npos) return false;
        
        pos = json.find(":", pos);
        if (pos == std::string::npos) return false;
        pos++;
        
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
            pos++;
        }
        
        if (pos >= json.size()) return false;
        
        if (json[pos] == '"') {
            // String value
            pos++;
            size_t end = json.find("\"", pos);
            if (end == std::string::npos) return false;
            value = json.substr(pos, end - pos);
            return true;
        } else if (json[pos] == '{') {
            // Object value - extract nested field
            size_t brace_count = 1;
            pos++;
            while (pos < json.size() && brace_count > 0) {
                if (json[pos] == '{') brace_count++;
                else if (json[pos] == '}') brace_count--;
                pos++;
            }
            return false; // Not implemented for nested objects in this simple parser
        }
        
        return false;
    }
    
    bool json_parse_nested_string(const std::string& json, const std::string& parent, const std::string& key, std::string& value) {
        std::string search_parent = "\"" + parent + "\"";
        size_t pos = json.find(search_parent);
        if (pos == std::string::npos) return false;
        
        pos = json.find(":", pos);
        if (pos == std::string::npos) return false;
        pos++;
        
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
            pos++;
        }
        
        if (pos >= json.size() || json[pos] != '{') return false;
        
        // Find matching closing brace
        size_t brace_count = 1;
        size_t obj_start = pos;
        pos++;
        while (pos < json.size() && brace_count > 0) {
            if (json[pos] == '{') brace_count++;
            else if (json[pos] == '}') brace_count--;
            pos++;
        }
        
        std::string obj_content = json.substr(obj_start, pos - obj_start);
        return json_parse_string(obj_content, key, value);
    }
}

// Helper to create a valid provenance snapshot for testing
// Uses distinct valid SHA-256 hashes (exactly 64 lowercase hex chars each,
// no reuse across fields).
pulse::PolicyProvenance create_test_provenance() {
    pulse::PolicyProvenance provenance;
    provenance.bundle_sha256 =        "460e98de7a77e97fc81d51244f17955ba3448542e8dc4762fff1e716be19f38a";
    provenance.manifest_sha256 =      "1a2b3c4d5e6f7890abcdef1234567890abcdef1234567890abcdef1234567890";
    provenance.checkpoint_sha256 =    "2b3c4d5e6f7890a1bcdef0123456789abcdef0123456789abcdef0123456789a";
    provenance.certification_sha256 = "3c4d5e6f7890a1b2cdef01234567890abcdef0123456789abcdef0123456789a";
    provenance.dll_sha256 =           "4d5e6f7890a1b2c3def01234567890abcdef0123456789abcdef0123456789ab";
    provenance.config_sha256 =        "5e6f7890a1b2c3d4ef01234567890abcdef0123456789abcdef0123456789abc";
    provenance.simulator_sha256 =     "6f7890a1b2c3d4e5f01234567890abcdef0123456789abcdef0123456789abcd";
    provenance.ruleset_sha256 =       "7890a1b2c3d4e5f601234567890abcdef0123456789abcdef0123456789abcde";
    return provenance;
}

void test_schedule_generation() {
    std::cout << "Testing schedule generation..." << std::endl;
    
    // Use a unique temp directory
    std::string temp_dir = "test_temp_schedule_gen_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    // Initialize with provenance snapshot in non-competitive mode
    if (!session.initialize(create_test_provenance(), false, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    // Test that we can get schedule for game indices
    for (int i = 0; i < 20; ++i) {
        auto sched = session.get_schedule(i);
        REQUIRE_EQ(sched.game_index, i, "game_index mismatch");
        REQUIRE_EQ(sched.human_physical_seat, i % 2, "human_physical_seat mismatch");
        REQUIRE_EQ(sched.bot_seat, 1 - sched.human_physical_seat, "bot_seat mismatch");
    }
    
    fs::remove_all(temp_dir);
    std::cout << "Schedule generation test: PASSED" << std::endl;
}

void test_exact_20_games() {
    std::cout << "Testing exact 20 game schedule..." << std::endl;
    
    std::string temp_dir = "test_temp_exact20_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    if (!session.initialize(create_test_provenance(), false, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    REQUIRE_EQ(session.total_games(), 20, "total_games should be 20");
    
    fs::remove_all(temp_dir);
    std::cout << "Exact 20 games test: PASSED" << std::endl;
}

void test_variable_game_count() {
    std::cout << "Testing variable game count..." << std::endl;
    
    std::string temp_dir = "test_temp_variable_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    if (!session.initialize(create_test_provenance(), true, 6)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    REQUIRE_EQ(session.total_games(), 6, "total_games should be 6");
    
    fs::remove_all(temp_dir);
    std::cout << "Variable game count test: PASSED" << std::endl;
}

void test_seat_alternation() {
    std::cout << "Testing seat alternation (10 each)..." << std::endl;
    
    // Human physical seat should alternate: 0,1,0,1,... for 20 games
    // This gives exactly 10 games in each seat
    int seat_0_count = 0;
    int seat_1_count = 0;
    
    std::string temp_dir = "test_temp_seat_alt_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    if (!session.initialize(create_test_provenance(), false, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    for (int i = 0; i < 20; ++i) {
        auto sched = session.get_schedule(i);
        int human_seat = sched.human_physical_seat;
        if (human_seat == 0) seat_0_count++;
        else seat_1_count++;
    }
    
    REQUIRE_EQ(seat_0_count, 10, "seat_0_count should be 10");
    REQUIRE_EQ(seat_1_count, 10, "seat_1_count should be 10");
    
    fs::remove_all(temp_dir);
    std::cout << "Seat alternation test: PASSED" << std::endl;
}

void test_nine_pairs_sequence() {
    std::cout << "Testing nine-pair Latin sequence..." << std::endl;
    
    // The first 18 games should use the nine-pair sequence twice
    // Games 0-8: first cycle, games 9-17: second cycle
    // Games 18-19: repeat (Kite,Kite) and (Vale,Vale)
    
    std::string temp_dir = "test_temp_ninepairs_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    if (!session.initialize(create_test_provenance(), false, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    // Check first 18 games follow nine-pair sequence
    const std::array<std::pair<pulse::Character, pulse::Character>, 9> expected_pairs = {
        std::make_pair(pulse::Character::Kite, pulse::Character::Kite),
        std::make_pair(pulse::Character::Vale, pulse::Character::Vale),
        std::make_pair(pulse::Character::Bastion, pulse::Character::Bastion),
        std::make_pair(pulse::Character::Kite, pulse::Character::Vale),
        std::make_pair(pulse::Character::Vale, pulse::Character::Bastion),
        std::make_pair(pulse::Character::Bastion, pulse::Character::Kite),
        std::make_pair(pulse::Character::Kite, pulse::Character::Bastion),
        std::make_pair(pulse::Character::Vale, pulse::Character::Kite),
        std::make_pair(pulse::Character::Bastion, pulse::Character::Vale)
    };
    
    for (int i = 0; i < 18; ++i) {
        auto sched = session.get_schedule(i);
        int pair_idx = i % 9;
        (void)pair_idx;  // suppress unused warning
        REQUIRE(sched.human_char == expected_pairs[pair_idx].first, "human_char mismatch in nine-pair sequence");
        REQUIRE(sched.bot_char == expected_pairs[pair_idx].second, "bot_char mismatch in nine-pair sequence");
    }
    
    // Check games 18 and 19
    auto sched18 = session.get_schedule(18);
    REQUIRE(sched18.human_char == pulse::Character::Kite, "game 18 human_char should be Kite");
    REQUIRE(sched18.bot_char == pulse::Character::Kite, "game 18 bot_char should be Kite");
    
    auto sched19 = session.get_schedule(19);
    REQUIRE(sched19.human_char == pulse::Character::Vale, "game 19 human_char should be Vale");
    REQUIRE(sched19.bot_char == pulse::Character::Vale, "game 19 bot_char should be Vale");
    
    fs::remove_all(temp_dir);
    std::cout << "Nine-pair sequence test: PASSED" << std::endl;
}

void test_character_marginals() {
    std::cout << "Testing character marginals (per role 7/7/6, combined 14/14/12)..." << std::endl;
    
    std::string temp_dir = "test_temp_marginals_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    if (!session.initialize(create_test_provenance(), false, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    // Count per role (human position only)
    int human_kite_count = 0;
    int human_vale_count = 0;
    int human_bastion_count = 0;
    
    int bot_kite_count = 0;
    int bot_vale_count = 0;
    int bot_bastion_count = 0;
    
    for (int i = 0; i < 20; ++i) {
        auto sched = session.get_schedule(i);
        if (sched.human_char == pulse::Character::Kite) human_kite_count++;
        if (sched.human_char == pulse::Character::Vale) human_vale_count++;
        if (sched.human_char == pulse::Character::Bastion) human_bastion_count++;
        
        if (sched.bot_char == pulse::Character::Kite) bot_kite_count++;
        if (sched.bot_char == pulse::Character::Vale) bot_vale_count++;
        if (sched.bot_char == pulse::Character::Bastion) bot_bastion_count++;
    }
    
    // Per role: 7/7/6 distribution
    REQUIRE_EQ(human_kite_count, 7, "human Kite count should be 7");
    REQUIRE_EQ(human_vale_count, 7, "human Vale count should be 7");
    REQUIRE_EQ(human_bastion_count, 6, "human Bastion count should be 6");
    
    REQUIRE_EQ(bot_kite_count, 7, "bot Kite count should be 7");
    REQUIRE_EQ(bot_vale_count, 7, "bot Vale count should be 7");
    REQUIRE_EQ(bot_bastion_count, 6, "bot Bastion count should be 6");
    
    // Combined: 14/14/12 (human + bot)
    REQUIRE_EQ(human_kite_count + bot_kite_count, 14, "combined Kite count should be 14");
    REQUIRE_EQ(human_vale_count + bot_vale_count, 14, "combined Vale count should be 14");
    REQUIRE_EQ(human_bastion_count + bot_bastion_count, 12, "combined Bastion count should be 12");
    
    fs::remove_all(temp_dir);
    std::cout << "Character marginals test: PASSED" << std::endl;
}

void test_directory_must_not_exist() {
    std::cout << "Testing directory must not exist..." << std::endl;
    
    std::string temp_dir = "test_temp_dir_exist_" + std::to_string(std::time(nullptr));
    
    // Create directory first
    fs::create_directory(temp_dir);
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    // Should fail because directory already exists
    REQUIRE(!session.initialize(create_test_provenance(), true, 20), "initialize should fail when directory exists");
    
    fs::remove_all(temp_dir);
    std::cout << "Directory must not exist test: PASSED" << std::endl;
}

void test_evidence_validation() {
    std::cout << "Testing evidence validation..." << std::endl;
    
    std::string temp_dir = "test_temp_evidence_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    if (!session.initialize(create_test_provenance(), false, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    // Test valid evidence
    pulse::challenge::GameEvidence valid_ev;
    valid_ev.game_index = 0;
    auto sched = session.get_schedule(0);
    valid_ev.human_seat = sched.human_physical_seat;
    valid_ev.bot_seat = sched.bot_seat;
    valid_ev.human_char = sched.human_char;
    valid_ev.bot_char = sched.bot_char;
    valid_ev.winner = 0;
    valid_ev.score_0 = 1;
    valid_ev.score_1 = 0;
    valid_ev.ticks = 1000;
    valid_ev.state_hash = 12345;
    valid_ev.completed = true;
    valid_ev.integrity_flags = false;
    valid_ev.termination_reason = "goal";
    
    REQUIRE(session.record_game(valid_ev), "should record valid evidence");
    
    // Test duplicate game_index (should fail)
    REQUIRE(!session.record_game(valid_ev), "should reject duplicate game_index");
    
    // Test out-of-order game_index (should fail)
    pulse::challenge::GameEvidence out_of_order;
    out_of_order = valid_ev;
    out_of_order.game_index = 5;  // skip ahead
    REQUIRE(!session.record_game(out_of_order), "should reject out-of-order game_index");
    
    // Test wrong schedule match (should fail)
    pulse::challenge::GameEvidence wrong_sched;
    wrong_sched = valid_ev;
    wrong_sched.game_index = 1;
    wrong_sched.human_seat = 2;  // wrong seat
    wrong_sched.termination_reason = "goal";
    REQUIRE(!session.record_game(wrong_sched), "should reject wrong schedule match");
    
    fs::remove_all(temp_dir);
    std::cout << "Evidence validation test: PASSED" << std::endl;
}

void test_gate_calculation() {
    std::cout << "Testing gate calculation..." << std::endl;
    
    std::string temp_dir = "test_temp_gate_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    // Initialize in COMPETITIVE mode for gate calculation test
    if (!session.initialize(create_test_provenance(), true, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    // Initially gate should not pass
    REQUIRE(!session.calculate_gate(), "gate should not pass initially");
    
    // Record 14 bot wins, 20 games, 10/10 seats
    for (int i = 0; i < 20; ++i) {
        auto sched = session.get_schedule(i);
        pulse::challenge::GameEvidence ev;
        ev.game_index = i;
        ev.human_seat = sched.human_physical_seat;
        ev.bot_seat = sched.bot_seat;
        ev.human_char = sched.human_char;
        ev.bot_char = sched.bot_char;
        ev.winner = sched.bot_seat;  // bot wins
        ev.score_0 = (sched.bot_seat == 0) ? 1 : 0;
        ev.score_1 = (sched.bot_seat == 1) ? 1 : 0;
        ev.ticks = 1000;
        ev.state_hash = 12345;
        ev.completed = true;
        ev.integrity_flags = false;
        ev.termination_reason = "goal";
        
        if (!session.record_game(ev)) {
            fs::remove_all(temp_dir);
            std::cerr << "Failed to record game " << i << std::endl;
            std::exit(1);
        }
    }
    
    // Now gate should pass
    REQUIRE(session.calculate_gate(), "gate should pass with 20 bot wins and 20 games in competitive mode");
    
    fs::remove_all(temp_dir);
    std::cout << "Gate calculation test: PASSED" << std::endl;
}

void test_timestamp_utc() {
    std::cout << "Testing UTC timestamp format..." << std::endl;
    
    std::string ts = pulse::challenge::ChallengeSession::get_timestamp_utc();
    
    // Should end with Z
    REQUIRE(ts.back() == 'Z', "timestamp should end with Z");
    
    // Should have T separator
    REQUIRE(ts.find('T') != std::string::npos, "timestamp should have T separator");
    
    std::cout << "UTC timestamp test: PASSED" << std::endl;
}

void test_helpers_public() {
    std::cout << "Testing public helpers..." << std::endl;
    
    // Test compute_simple_hash
    std::string hash1 = pulse::challenge::ChallengeSession::compute_simple_hash("test");
    std::string hash2 = pulse::challenge::ChallengeSession::compute_simple_hash("test");
    std::string hash3 = pulse::challenge::ChallengeSession::compute_simple_hash("different");
    
    REQUIRE(hash1 == hash2, "hash should be deterministic");  // deterministic
    REQUIRE(hash1 != hash3, "different input should produce different hash");  // different input
    
    std::cout << "Public helpers test: PASSED" << std::endl;
}

void test_game20_freeze() {
    std::cout << "Testing game 20 freeze behavior..." << std::endl;
    
    std::string temp_dir = "test_temp_game20_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    // Initialize in COMPETITIVE mode for gate calculation test
    if (!session.initialize(create_test_provenance(), true, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    // Record all 20 games
    for (int i = 0; i < 20; ++i) {
        auto sched = session.get_schedule(i);
        pulse::challenge::GameEvidence ev;
        ev.game_index = i;
        ev.human_seat = sched.human_physical_seat;
        ev.bot_seat = sched.bot_seat;
        ev.human_char = sched.human_char;
        ev.bot_char = sched.bot_char;
        ev.winner = sched.bot_seat;  // bot wins
        ev.score_0 = (sched.bot_seat == 0) ? 1 : 0;
        ev.score_1 = (sched.bot_seat == 1) ? 1 : 0;
        ev.ticks = 1000;
        ev.state_hash = 12345;
        ev.completed = true;
        ev.integrity_flags = false;
        ev.termination_reason = "goal";
        
        if (!session.record_game(ev)) {
            fs::remove_all(temp_dir);
            std::cerr << "Failed to record game " << i << std::endl;
            std::exit(1);
        }
    }
    
    // Session should be complete
    REQUIRE(session.is_complete(), "session should be complete after 20 games");
    REQUIRE_EQ(session.current_game_index(), 20, "current_game_index should be 20");
    
    // Gate should pass with 20 bot wins
    REQUIRE(session.calculate_gate(), "gate should pass with 20 bot wins");
    
    fs::remove_all(temp_dir);
    std::cout << "Game 20 freeze test: PASSED" << std::endl;
}

void test_integrity_error_handling() {
    std::cout << "Testing integrity error handling..." << std::endl;
    
    std::string temp_dir = "test_temp_integrity_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    if (!session.initialize(create_test_provenance(), false, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    // Record a game with integrity error should be rejected (completed=true with integrity_flags=true)
    auto sched = session.get_schedule(0);
    pulse::challenge::GameEvidence ev;
    ev.game_index = 0;
    ev.human_seat = sched.human_physical_seat;
    ev.bot_seat = sched.bot_seat;
    ev.human_char = sched.human_char;
    ev.bot_char = sched.bot_char;
    ev.winner = sched.bot_seat;
    ev.score_0 = (sched.bot_seat == 0) ? 1 : 0;
    ev.score_1 = (sched.bot_seat == 1) ? 1 : 0;
    ev.ticks = 1000;
    ev.state_hash = 12345;
    ev.completed = true;
    ev.integrity_flags = true;  // integrity error
    ev.termination_reason = "goal";
    
    REQUIRE(!session.record_game(ev), "should reject completed game with integrity error");
    
    // Record a clean game
    pulse::challenge::GameEvidence clean_ev;
    clean_ev.game_index = 0;
    clean_ev.human_seat = sched.human_physical_seat;
    clean_ev.bot_seat = sched.bot_seat;
    clean_ev.human_char = sched.human_char;
    clean_ev.bot_char = sched.bot_char;
    clean_ev.winner = sched.bot_seat;
    clean_ev.score_0 = (sched.bot_seat == 0) ? 1 : 0;
    clean_ev.score_1 = (sched.bot_seat == 1) ? 1 : 0;
    clean_ev.ticks = 1000;
    clean_ev.state_hash = 12345;
    clean_ev.completed = true;
    clean_ev.integrity_flags = false;
    clean_ev.termination_reason = "goal";
    
    REQUIRE(session.record_game(clean_ev), "should record clean evidence");
    
    // Mark integrity violation via API
    session.mark_integrity_violation("test violation");
    
    // Gate should not pass with integrity violation
    REQUIRE(!session.calculate_gate(), "gate should not pass with integrity violation");
    REQUIRE(!session.summary().results_integrity, "results_integrity should be false");
    
    fs::remove_all(temp_dir);
    std::cout << "Integrity error handling test: PASSED" << std::endl;
}

void test_partial_session_summary() {
    std::cout << "Testing partial session summary..." << std::endl;
    
    std::string temp_dir = "test_temp_partial_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    if (!session.initialize(create_test_provenance(), false, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    // Record only 5 games (partial session)
    for (int i = 0; i < 5; ++i) {
        auto sched = session.get_schedule(i);
        pulse::challenge::GameEvidence ev;
        ev.game_index = i;
        ev.human_seat = sched.human_physical_seat;
        ev.bot_seat = sched.bot_seat;
        ev.human_char = sched.human_char;
        ev.bot_char = sched.bot_char;
        ev.winner = sched.bot_seat;
        ev.score_0 = (sched.bot_seat == 0) ? 1 : 0;
        ev.score_1 = (sched.bot_seat == 1) ? 1 : 0;
        ev.ticks = 1000;
        ev.state_hash = 12345;
        ev.completed = true;
        ev.integrity_flags = false;
        ev.termination_reason = "goal";
        
        if (!session.record_game(ev)) {
            fs::remove_all(temp_dir);
            std::cerr << "Failed to record game " << i << std::endl;
            std::exit(1);
        }
    }
    
    // Session should not be complete
    REQUIRE(!session.is_complete(), "session should not be complete after 5 games");
    REQUIRE_EQ(session.current_game_index(), 5, "current_game_index should be 5");
    
    // Write summary should succeed
    REQUIRE(session.write_summary(), "write_summary should succeed");
    
    // Check summary state
    const auto& summary = session.summary();
    REQUIRE_EQ(summary.completed_games, 5, "completed_games should be 5");
    REQUIRE(summary.completion_state == "PARTIAL", "completion_state should be PARTIAL");
    
    fs::remove_all(temp_dir);
    std::cout << "Partial session summary test: PASSED" << std::endl;
}

void test_json_escaping() {
    std::cout << "Testing JSON escaping..." << std::endl;
    
    // Test the json_escape function with various special characters
    std::string test1 = "simple";
    std::string escaped1 = pulse::challenge::ChallengeSession::json_escape(test1);
    REQUIRE(escaped1 == "simple", "simple string should escape to itself");
    
    std::string test2 = "quote\"test";
    std::string escaped2 = pulse::challenge::ChallengeSession::json_escape(test2);
    REQUIRE(escaped2 == "quote\\\"test", "quote should be escaped");
    
    std::string test3 = "backslash\\test";
    std::string escaped3 = pulse::challenge::ChallengeSession::json_escape(test3);
    REQUIRE(escaped3 == "backslash\\\\test", "backslash should be escaped");
    
    std::string test4 = "newline\ntest";
    std::string escaped4 = pulse::challenge::ChallengeSession::json_escape(test4);
    REQUIRE(escaped4 == "newline\\ntest", "newline should be escaped");
    
    std::string test5 = "tab\ttest";
    std::string escaped5 = pulse::challenge::ChallengeSession::json_escape(test5);
    REQUIRE(escaped5 == "tab\\ttest", "tab should be escaped");
    
    // Test Windows path escaping
    std::string test6 = "C:\\Users\\test";
    std::string escaped6 = pulse::challenge::ChallengeSession::json_escape(test6);
    REQUIRE(escaped6 == "C:\\\\Users\\\\test", "Windows path should be escaped");
    
    std::cout << "JSON escaping test: PASSED" << std::endl;
}

void test_six_game_smoke_mode() {
    std::cout << "Testing six-game smoke mode..." << std::endl;
    
    std::string temp_dir = "test_temp_sixgame_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    // Initialize with non-competitive mode and 6 games
    if (!session.initialize(create_test_provenance(), false, 6)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    REQUIRE_EQ(session.total_games(), 6, "total_games should be 6");
    REQUIRE(!session.summary().competitive_gate_applicable, "competitive_gate_applicable should be false");
    
    // Record 6 games
    for (int i = 0; i < 6; ++i) {
        auto sched = session.get_schedule(i);
        pulse::challenge::GameEvidence ev;
        ev.game_index = i;
        ev.human_seat = sched.human_physical_seat;
        ev.bot_seat = sched.bot_seat;
        ev.human_char = sched.human_char;
        ev.bot_char = sched.bot_char;
        ev.winner = sched.bot_seat;
        ev.score_0 = (sched.bot_seat == 0) ? 1 : 0;
        ev.score_1 = (sched.bot_seat == 1) ? 1 : 0;
        ev.ticks = 1000;
        ev.state_hash = 12345;
        ev.completed = true;
        ev.integrity_flags = false;
        ev.termination_reason = "goal";
        
        if (!session.record_game(ev)) {
            fs::remove_all(temp_dir);
            std::cerr << "Failed to record game " << i << std::endl;
            std::exit(1);
        }
    }
    
    // Session should be complete
    REQUIRE(session.is_complete(), "session should be complete after 6 games");
    REQUIRE_EQ(session.current_game_index(), 6, "current_game_index should be 6");
    
    // Check seat counts for 6 games (should be 3/3)
    const auto& summary = session.summary();
    REQUIRE_EQ(summary.human_seat_0_count, 3, "human_seat_0_count should be 3");
    REQUIRE_EQ(summary.human_seat_1_count, 3, "human_seat_1_count should be 3");
    
    // Gate should NOT pass (non-competitive mode)
    REQUIRE(!session.calculate_gate(), "gate should not pass in non-competitive mode");
    REQUIRE(!summary.gate_passed, "gate_passed should be false");
    
    // Completion state should be COMPLETE
    REQUIRE(summary.completion_state == "COMPLETE", "completion_state should be COMPLETE");
    
    fs::remove_all(temp_dir);
    std::cout << "Six-game smoke mode test: PASSED" << std::endl;
}

void test_evidence_semantics_validation() {
    std::cout << "Testing evidence semantics validation..." << std::endl;
    
    std::string temp_dir = "test_temp_semantics_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    if (!session.initialize(create_test_provenance(), false, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    auto sched = session.get_schedule(0);
    
    // Test invalid score-to-one (scores don't match termination)
    pulse::challenge::GameEvidence invalid_scores;
    invalid_scores.game_index = 0;
    invalid_scores.human_seat = sched.human_physical_seat;
    invalid_scores.bot_seat = sched.bot_seat;
    invalid_scores.human_char = sched.human_char;
    invalid_scores.bot_char = sched.bot_char;
    invalid_scores.winner = 0;
    invalid_scores.score_0 = 2;  // Invalid: should be 1 for score-to-one
    invalid_scores.score_1 = 0;
    invalid_scores.ticks = 1000;
    invalid_scores.state_hash = 12345;
    invalid_scores.completed = true;
    invalid_scores.integrity_flags = false;
    invalid_scores.termination_reason = "goal";
    
    REQUIRE(!session.record_game(invalid_scores), "should reject invalid score-to-one");
    
    // Test winner/score mismatch
    pulse::challenge::GameEvidence winner_mismatch;
    winner_mismatch.game_index = 0;
    winner_mismatch.human_seat = sched.human_physical_seat;
    winner_mismatch.bot_seat = sched.bot_seat;
    winner_mismatch.human_char = sched.human_char;
    winner_mismatch.bot_char = sched.bot_char;
    winner_mismatch.winner = 1;  // Bot won
    winner_mismatch.score_0 = 1;  // But seat 0 has higher score
    winner_mismatch.score_1 = 0;
    winner_mismatch.ticks = 1000;
    winner_mismatch.state_hash = 12345;
    winner_mismatch.completed = true;
    winner_mismatch.integrity_flags = false;
    winner_mismatch.termination_reason = "goal";
    
    REQUIRE(!session.record_game(winner_mismatch), "should reject winner/score mismatch");
    
    // Test timeout draw with non-zero scores
    pulse::challenge::GameEvidence timeout_scores;
    timeout_scores.game_index = 0;
    timeout_scores.human_seat = sched.human_physical_seat;
    timeout_scores.bot_seat = sched.bot_seat;
    timeout_scores.human_char = sched.human_char;
    timeout_scores.bot_char = sched.bot_char;
    timeout_scores.winner = -1;
    timeout_scores.score_0 = 1;  // Invalid: should be 0 for timeout draw
    timeout_scores.score_1 = 0;
    timeout_scores.ticks = 12000;
    timeout_scores.state_hash = 12345;
    timeout_scores.completed = true;
    timeout_scores.integrity_flags = false;
    timeout_scores.termination_reason = "timeout_draw";
    
    REQUIRE(!session.record_game(timeout_scores), "should reject timeout draw with non-zero scores");
    
    // Test tick bounds
    pulse::challenge::GameEvidence invalid_ticks;
    invalid_ticks.game_index = 0;
    invalid_ticks.human_seat = sched.human_physical_seat;
    invalid_ticks.bot_seat = sched.bot_seat;
    invalid_ticks.human_char = sched.human_char;
    invalid_ticks.bot_char = sched.bot_char;
    invalid_ticks.winner = -1;
    invalid_ticks.score_0 = 0;
    invalid_ticks.score_1 = 0;
    invalid_ticks.ticks = 13000;  // Invalid: exceeds max
    invalid_ticks.state_hash = 12345;
    invalid_ticks.completed = true;
    invalid_ticks.integrity_flags = false;
    invalid_ticks.termination_reason = "timeout_draw";
    
    REQUIRE(!session.record_game(invalid_ticks), "should reject ticks exceeding maximum");
    
    fs::remove_all(temp_dir);
    std::cout << "Evidence semantics validation test: PASSED" << std::endl;
}

void test_integrity_gate_aggregation() {
    std::cout << "Testing integrity gate aggregation..." << std::endl;
    
    std::string temp_dir = "test_temp_integrity_gate_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    if (!session.initialize(create_test_provenance(), true, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    // Record 19 games with clean integrity
    for (int i = 0; i < 19; ++i) {
        auto sched = session.get_schedule(i);
        pulse::challenge::GameEvidence ev;
        ev.game_index = i;
        ev.human_seat = sched.human_physical_seat;
        ev.bot_seat = sched.bot_seat;
        ev.human_char = sched.human_char;
        ev.bot_char = sched.bot_char;
        ev.winner = sched.bot_seat;
        ev.score_0 = (sched.bot_seat == 0) ? 1 : 0;
        ev.score_1 = (sched.bot_seat == 1) ? 1 : 0;
        ev.ticks = 1000;
        ev.state_hash = 12345;
        ev.completed = true;
        ev.integrity_flags = false;
        ev.termination_reason = "goal";
        
        if (!session.record_game(ev)) {
            fs::remove_all(temp_dir);
            std::cerr << "Failed to record game " << i << std::endl;
            std::exit(1);
        }
    }
    
    // Record 1 game with clean integrity (can't record with integrity_flags=true anymore)
    auto sched = session.get_schedule(19);
    pulse::challenge::GameEvidence clean_ev;
    clean_ev.game_index = 19;
    clean_ev.human_seat = sched.human_physical_seat;
    clean_ev.bot_seat = sched.bot_seat;
    clean_ev.human_char = sched.human_char;
    clean_ev.bot_char = sched.bot_char;
    clean_ev.winner = sched.bot_seat;
    clean_ev.score_0 = (sched.bot_seat == 0) ? 1 : 0;
    clean_ev.score_1 = (sched.bot_seat == 1) ? 1 : 0;
    clean_ev.ticks = 1000;
    clean_ev.state_hash = 12345;
    clean_ev.completed = true;
    clean_ev.integrity_flags = false;
    clean_ev.termination_reason = "goal";
    
    REQUIRE(session.record_game(clean_ev), "should record clean game");
    
    // Mark integrity violation via API instead
    session.mark_integrity_violation("test integrity violation");
    
    // Gate should fail due to integrity violation
    REQUIRE(!session.calculate_gate(), "gate should fail with integrity violation");
    REQUIRE(!session.summary().results_integrity, "results_integrity should be false");
    
    fs::remove_all(temp_dir);
    std::cout << "Integrity gate aggregation test: PASSED" << std::endl;
}

void test_timeout_termination() {
    std::cout << "Testing timeout termination..." << std::endl;
    
    std::string temp_dir = "test_temp_timeout_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    
    if (!session.initialize(create_test_provenance(), false, 20)) {
        fs::remove_all(temp_dir);
        std::cerr << "Failed to initialize session" << std::endl;
        std::exit(1);
    }
    
    auto sched = session.get_schedule(0);
    
    // Test valid timeout draw
    pulse::challenge::GameEvidence timeout_ev;
    timeout_ev.game_index = 0;
    timeout_ev.human_seat = sched.human_physical_seat;
    timeout_ev.bot_seat = sched.bot_seat;
    timeout_ev.human_char = sched.human_char;
    timeout_ev.bot_char = sched.bot_char;
    timeout_ev.winner = -1;  // draw
    timeout_ev.score_0 = 0;  // both zero for timeout draw
    timeout_ev.score_1 = 0;
    timeout_ev.ticks = 12000;  // max ticks
    timeout_ev.state_hash = 12345;
    timeout_ev.completed = true;
    timeout_ev.integrity_flags = false;
    timeout_ev.termination_reason = "timeout_draw";
    
    REQUIRE(session.record_game(timeout_ev), "should record valid timeout draw");
    
    fs::remove_all(temp_dir);
    std::cout << "Timeout termination test: PASSED" << std::endl;
}

void test_provenance_validation() {
    std::cout << "Testing provenance validation..." << std::endl;
    
    std::string temp_dir = "test_temp_provenance_" + std::to_string(std::time(nullptr));
    
    // Test 1: Missing required field (empty checkpoint_sha256)
    pulse::PolicyProvenance prov1 = create_test_provenance();
    prov1.checkpoint_sha256.clear();
    
    pulse::challenge::ChallengeSession session1(temp_dir);
    REQUIRE(!session1.initialize(prov1, false, 20), "should reject provenance with missing checkpoint_sha256");
    
    fs::remove_all(temp_dir);
    
    // Test 2: Invalid SHA-256 format (not 64 hex chars)
    std::string temp_dir2 = "test_temp_provenance2_" + std::to_string(std::time(nullptr));
    pulse::PolicyProvenance prov2 = create_test_provenance();
    prov2.bundle_sha256 = "invalid_hash_not_64_chars";
    
    pulse::challenge::ChallengeSession session2(temp_dir2);
    REQUIRE(!session2.initialize(prov2, false, 20), "should reject provenance with invalid SHA-256 format");
    
    fs::remove_all(temp_dir2);
    
    // Test 3: SHA-256 with uppercase (should be lowercase)
    std::string temp_dir3 = "test_temp_provenance3_" + std::to_string(std::time(nullptr));
    pulse::PolicyProvenance prov3 = create_test_provenance();
    prov3.certification_sha256 = "460E98DE7A77E97FC81D51244F17955BA3448542E8DC4762FFF1E716BE19F38A";
    
    pulse::challenge::ChallengeSession session3(temp_dir3);
    REQUIRE(!session3.initialize(prov3, false, 20), "should reject provenance with uppercase SHA-256");
    
    fs::remove_all(temp_dir3);
    
    // Test 4: SHA-256 with non-hex characters
    std::string temp_dir4 = "test_temp_provenance4_" + std::to_string(std::time(nullptr));
    pulse::PolicyProvenance prov4 = create_test_provenance();
    prov4.dll_sha256 = "460e98de7a77e97fc81d51244f17955ba3448542e8dc4762fff1e716be19f38g";  // 'g' is invalid
    
    pulse::challenge::ChallengeSession session4(temp_dir4);
    REQUIRE(!session4.initialize(prov4, false, 20), "should reject provenance with non-hex characters");
    
    fs::remove_all(temp_dir4);
    
    // Test 5: SHA-256 with 63 characters (too short)
    std::string temp_dir5 = "test_temp_provenance5_" + std::to_string(std::time(nullptr));
    pulse::PolicyProvenance prov5 = create_test_provenance();
    prov5.manifest_sha256 = "460e98de7a77e97fc81d51244f17955ba3448542e8dc4762fff1e716be19f38";  // 63 chars
    
    pulse::challenge::ChallengeSession session5(temp_dir5);
    REQUIRE(!session5.initialize(prov5, false, 20), "should reject provenance with 63-character SHA-256");
    
    fs::remove_all(temp_dir5);
    
    // Test 6: SHA-256 with too many characters (too long)
    std::string temp_dir6 = "test_temp_provenance6_" + std::to_string(std::time(nullptr));
    pulse::PolicyProvenance prov6 = create_test_provenance();
    prov6.bundle_sha256 = "460e98de7a77e97fc81d51244f17955ba3448542e8dc4762fff1e716be19f38a00";  // 66 chars (too long)
    
    pulse::challenge::ChallengeSession session6(temp_dir6);
    REQUIRE(!session6.initialize(prov6, false, 20), "should reject provenance with too-long SHA-256");
    
    fs::remove_all(temp_dir6);
    
    // Test 7: Valid provenance should succeed
    std::string temp_dir7 = "test_temp_provenance7_" + std::to_string(std::time(nullptr));
    pulse::PolicyProvenance prov7 = create_test_provenance();
    
    pulse::challenge::ChallengeSession session7(temp_dir7);
    REQUIRE(session7.initialize(prov7, false, 20), "should accept valid provenance");
    
    fs::remove_all(temp_dir7);
    std::cout << "Provenance validation test: PASSED" << std::endl;
}

void test_no_overwrite_protection() {
    std::cout << "Testing no-overwrite protection..." << std::endl;
    
    std::string temp_dir = "test_temp_nooverwrite_" + std::to_string(std::time(nullptr));
    
    // First initialization should succeed
    pulse::challenge::ChallengeSession session1(temp_dir);
    REQUIRE(session1.initialize(create_test_provenance(), false, 20), "first initialization should succeed");
    
    // Second initialization with same directory should fail (directory exists)
    pulse::challenge::ChallengeSession session2(temp_dir);
    REQUIRE(!session2.initialize(create_test_provenance(), false, 20), "second initialization should fail (directory exists)");
    
    fs::remove_all(temp_dir);
    std::cout << "No-overwrite protection test: PASSED" << std::endl;
}

void test_session_json_provenance() {
    std::cout << "Testing session JSON provenance..." << std::endl;
    
    std::string temp_dir = "test_temp_sessionjson_" + std::to_string(std::time(nullptr));
    
    pulse::challenge::ChallengeSession session(temp_dir);
    REQUIRE(session.initialize(create_test_provenance(), false, 20), "initialization should succeed");
    
    // Read the session JSON file
    std::string session_json_path = temp_dir + "/challenge_session.json";
    std::ifstream session_file(session_json_path);
    REQUIRE(session_file.is_open(), "session JSON file should exist");
    
    std::string session_json((std::istreambuf_iterator<char>(session_file)),
                            std::istreambuf_iterator<char>());
    session_file.close();
    
    // Validate provenance fields in session JSON using our parser.
    // The session JSON uses flat field names (bundle_sha256, manifest_sha256,
    // checkpoint_sha, certification_sha, dll_sha), not nested objects.
    std::string bundle_sha, manifest_sha, checkpoint_sha, cert_sha, dll_sha;
    
    REQUIRE(json_parse_string(session_json, "bundle_sha256", bundle_sha), "should parse bundle_sha256");
    REQUIRE(json_parse_string(session_json, "manifest_sha256", manifest_sha), "should parse manifest_sha256");
    REQUIRE(json_parse_string(session_json, "checkpoint_sha", checkpoint_sha), "should parse checkpoint_sha");
    REQUIRE(json_parse_string(session_json, "certification_sha", cert_sha), "should parse certification_sha");
    REQUIRE(json_parse_string(session_json, "dll_sha", dll_sha), "should parse dll_sha");
    
    // Validate SHA-256 format (64 lowercase hex chars)
    auto validate_sha256 = [](const std::string& sha) -> bool {
        if (sha.length() != 64) return false;
        for (char c : sha) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
        return true;
    };
    
    REQUIRE(validate_sha256(bundle_sha), "bundle_sha should be valid SHA-256");
    REQUIRE(validate_sha256(manifest_sha), "manifest_sha should be valid SHA-256");
    REQUIRE(validate_sha256(checkpoint_sha), "checkpoint_sha should be valid SHA-256");
    REQUIRE(validate_sha256(cert_sha), "cert_sha should be valid SHA-256");
    REQUIRE(validate_sha256(dll_sha), "dll_sha should be valid SHA-256");
    
    // Validate exact values match the fixture's distinct hashes.
    auto prov = create_test_provenance();
    REQUIRE_EQ(bundle_sha, prov.bundle_sha256, "bundle_sha should match provenance");
    REQUIRE_EQ(manifest_sha, prov.manifest_sha256, "manifest_sha should match provenance");
    REQUIRE_EQ(checkpoint_sha, prov.checkpoint_sha256, "checkpoint_sha should match provenance");
    REQUIRE_EQ(cert_sha, prov.certification_sha256, "cert_sha should match provenance");
    REQUIRE_EQ(dll_sha, prov.dll_sha256, "dll_sha should match provenance");
    
    fs::remove_all(temp_dir);
    std::cout << "Session JSON provenance test: PASSED" << std::endl;
}

int main() {
    pulse::test::disable_error_dialogs();
    std::cout << "=== Challenge Schedule Tests ===" << std::endl;
    
    test_schedule_generation();
    test_exact_20_games();
    test_variable_game_count();
    test_seat_alternation();
    test_nine_pairs_sequence();
    test_character_marginals();
    test_directory_must_not_exist();
    test_evidence_validation();
    test_gate_calculation();
    test_timestamp_utc();
    test_helpers_public();
    test_game20_freeze();
    test_integrity_error_handling();
    test_partial_session_summary();
    test_json_escaping();
    test_six_game_smoke_mode();
    test_evidence_semantics_validation();
    test_integrity_gate_aggregation();
    test_timeout_termination();
    test_provenance_validation();
    test_no_overwrite_protection();
    test_session_json_provenance();
    
    std::cout << "=== All schedule tests PASSED ===" << std::endl;
    return 0;
}
