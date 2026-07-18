#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Character is defined in pulse_sim.hpp, included via pulse_core dependency
namespace pulse {

// Forward declaration - Character is defined in pulse_sim.hpp as std::uint8_t
enum class Character : std::uint8_t;

enum class ChallengeStatus {
    Active,
    Partial,
    Complete,
    Pass,
    Fail
};

struct GameSchedule {
    std::array<Character, 2> characters;
    std::int8_t human_seat;
    std::int8_t bot_seat;
};

struct GameEvidence {
    std::int32_t game_index;
    std::array<Character, 2> characters;
    std::int8_t human_seat;
    std::int8_t bot_seat;
    std::int8_t winner;
    std::int8_t human_score;
    std::int8_t bot_score;
    std::string final_state_hash;
    std::string replay_path;
    // T4: set true when this game's bot seat was driven by the non-strict
    // preview policy path (pulse::ai::PolicyController over a load_preview()
    // bundle) instead of the certified/scripted paths. Propagated into
    // games.jsonl as "non_certified_preview" so preview evidence can never
    // be mistaken for certified challenge evidence.
    bool non_certified_preview = false;
};

struct SessionSummary {
    std::int32_t total_games;
    std::int32_t completed_games;
    std::int32_t human_wins;
    std::int32_t bot_wins;
    std::int32_t human_seat_0_count;
    std::int32_t human_seat_1_count;
    bool gate_passed;
    std::string bundle_sha;
    std::string manifest_sha;
    std::string checkpoint_id;
    std::string certification_sha;
    std::string dll_sha;
    ChallengeStatus status;
};

struct PolicyMetadata {
    std::string checkpoint_id;
    std::string model_version;
    std::string bundle_sha;
    std::string manifest_sha;
    std::string certification_sha;
};

class ChallengeSession {
public:
    static constexpr std::int32_t kTotalGames = 20;

    ChallengeSession(const PolicyMetadata& policy,
                     const std::filesystem::path& log_dir,
                     std::int32_t requested_games);

    bool valid() const noexcept { return valid_; }
    const std::string& error() const noexcept { return error_; }

    GameSchedule schedule_game(std::int32_t index) const noexcept;
    bool start_game(std::int32_t index);
    void record_game_result(const GameEvidence& evidence);
    void write_summary(ChallengeStatus status);
    SessionSummary summary() const noexcept;
    bool gate_passed() const noexcept;

    // State machine control for R reset/discard and WM_CLOSE/Esc paths
    bool can_reset() const noexcept;
    bool can_discard() const noexcept;
    void reset_session();
    void discard_session();

private:
    void build_schedule();
    void append_game_line(const GameEvidence& evidence);
    void flush();
    void write_session_json();
    void recompute_summary(SessionSummary& s) const;
    static const char* status_string(ChallengeStatus status) noexcept;
    static std::string json_escape(const std::string& s);

    PolicyMetadata policy_;
    std::filesystem::path log_dir_;
    std::int32_t requested_games_;
    std::vector<GameSchedule> schedule_;
    std::vector<GameEvidence> games_;
    std::int32_t current_game_index_ = -1;
    bool valid_ = false;
    std::string error_;
};

}  // namespace pulse
