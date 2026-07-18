#pragma once

// Shared challenge session/evidence component for Pulse Zero human challenge protocol.
// Used by both viewer and headless modes. Not Win32-render dependent.

#include "pulse_sim.hpp"
#include "pulse_policy.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace pulse::challenge {

struct GameSchedule {
    int game_index;
    int human_physical_seat;  // 0 or 1
    int bot_seat;             // 0 or 1
    Character human_char;
    Character bot_char;
};

struct GameEvidence {
    int game_index;
    int human_seat;
    int bot_seat;
    Character human_char;
    Character bot_char;
    int winner;               // 0, 1, or -1 (draw/unfinished)
    int score_0;
    int score_1;
    int ticks;
    std::uint64_t state_hash;
    std::string timestamp_utc;  // ISO 8601 with Z suffix
    bool completed;
    bool integrity_flags;       // restart attempts, protocol violations
    std::string termination_reason;  // "goal", "timeout_draw", "incomplete"
};

struct SessionSummary {
    int total_games;  // Set in constructor to 20, can be overridden in initialize
    int completed_games = 0;
    int bot_wins = 0;
    int human_wins = 0;
    int draws = 0;
    int human_seat_0_count = 0;
    int human_seat_1_count = 0;
    bool gate_passed = false;  // bot_wins >= 14 && completed_games == 20 && seat_counts == 10/10
    bool competitive_gate_applicable = true;  // true for 20-game viewer sessions, false for bounded smoke
    std::string start_timestamp_utc;
    std::string end_timestamp_utc;
    std::string bundle_sha256;
    std::string manifest_sha256;
    std::string checkpoint_sha;
    std::string certification_sha;
    std::string dll_sha;
    std::string ruleset_hash;
    std::string simulator_hash;
    std::string config_hash;
    std::string completion_state = "ACTIVE";  // ACTIVE/PARTIAL/COMPLETE
    bool schedule_integrity = true;
    bool results_integrity = true;
    bool provenance_integrity = true;
    
    // Required summary detail: schedule identity and outcomes by seat/matchup
    std::string schedule_identity;  // SHA-256 of schedule JSON
    int bot_seat_0_wins = 0;
    int bot_seat_1_wins = 0;
    int human_seat_0_wins = 0;
    int human_seat_1_wins = 0;
    
    // Matchup outcomes: key format "human_char_bot_char" (e.g., "Kite_Vale")
    // Each entry: {wins, losses, draws} from human perspective
    std::map<std::string, std::tuple<int, int, int>> matchup_outcomes;
};

class ChallengeSession {
public:
    ChallengeSession(const std::string& log_dir);
    
    // Initialize session with immutable schedule and provenance
    // Requires nonexistent output directory; writes schedule/session before game 1
    // competitive_mode: true for 20-game viewer sessions, false for bounded smoke
    // total_games_override: if > 0, overrides the default 20 games
    // provenance: immutable policy provenance snapshot from Policy::provenance()
    bool initialize(const pulse::PolicyProvenance& provenance, bool competitive_mode = true, int total_games_override = 0);
    
    // Get schedule for game index
    GameSchedule get_schedule(int game_index) const;
    
    // Record completed game evidence (append-only, flushed)
    // Validates exact next index, no duplicates, no out-of-order
    bool record_game(const GameEvidence& evidence);
    
    // Write partial/final summary (atomic replace)
    bool write_summary();
    
    // Calculate gate result (strict: wins>=14, completion20, exact seat counts, exact schedule/result identity)
    bool calculate_gate() const;
    
    // Mark integrity violation (called by viewer on R/reset/discard)
    // Sets results_integrity to false and freezes the session
    void mark_integrity_violation(const std::string& reason);
    
    // Session state
    int current_game_index() const;
    bool is_complete() const;
    
    // Access summary for HUD
    const SessionSummary& summary() const { return summary_; }
    int total_games() const { return summary_.total_games; }
    
    // Testable helpers
    static std::string get_timestamp_utc();
    static std::string compute_simple_hash(const std::string& data);
    static std::string json_escape(const std::string& s);
    
private:
    std::string log_dir_;
    std::vector<GameSchedule> schedule_;
    std::vector<GameEvidence> evidence_;
    SessionSummary summary_;
    pulse::PolicyProvenance provenance_;
    int current_game_index_;
    bool initialized_;
    bool integrity_violated_;  // Set by mark_integrity_violation
    
    // Generate frozen 20-game schedule
    void generate_schedule();
    
    // Write session JSON (immutable, before game 1)
    bool write_session_json();
    
    // Append game evidence to JSONL file with strict flush
    bool append_game_jsonl(const GameEvidence& evidence);
    
    // Validate evidence matches schedule exactly
    bool validate_evidence_against_schedule(const GameEvidence& evidence) const;
    
    // Validate evidence semantics (scores, winner, termination, ticks)
    bool validate_evidence_semantics(const GameEvidence& evidence) const;
    
    // Atomic write-through summary
    bool write_summary_atomic();
};

} // namespace pulse::challenge
