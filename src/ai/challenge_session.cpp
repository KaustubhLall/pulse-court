#include "challenge_session.hpp"
#include "pulse_sim.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <io.h>
#include <fcntl.h>
#include <windows.h>

namespace pulse {

namespace {

const GameSchedule kLatinSequence[9] = {
    GameSchedule{{Character::Kite, Character::Kite}, 0, 1},
    GameSchedule{{Character::Vale, Character::Vale}, 0, 1},
    GameSchedule{{Character::Bastion, Character::Bastion}, 0, 1},
    GameSchedule{{Character::Kite, Character::Vale}, 0, 1},
    GameSchedule{{Character::Vale, Character::Bastion}, 0, 1},
    GameSchedule{{Character::Bastion, Character::Kite}, 0, 1},
    GameSchedule{{Character::Kite, Character::Bastion}, 0, 1},
    GameSchedule{{Character::Vale, Character::Kite}, 0, 1},
    GameSchedule{{Character::Bastion, Character::Vale}, 0, 1},
};

}  // namespace

ChallengeSession::ChallengeSession(const PolicyMetadata& policy,
                                   const std::filesystem::path& log_dir,
                                   std::int32_t requested_games)
    : policy_(policy), log_dir_(log_dir), requested_games_(requested_games) {
    if (requested_games_ <= 0 || requested_games_ > kTotalGames) {
        error_ = "requested_games out of range";
        return;
    }

    build_schedule();

    if (std::filesystem::exists(log_dir_)) {
        if (std::filesystem::exists(log_dir_ / "session.json") ||
            std::filesystem::exists(log_dir_ / "games.jsonl") ||
            std::filesystem::exists(log_dir_ / "summary.json")) {
            error_ = "log directory already contains challenge evidence";
            return;
        }
    } else {
        std::error_code ec;
        std::filesystem::create_directories(log_dir_, ec);
        if (ec) {
            error_ = "failed to create log directory: " + ec.message();
            return;
        }
    }

    valid_ = true;
    write_session_json();
}

void ChallengeSession::build_schedule() {
    schedule_.clear();
    for (std::int32_t i = 0; i < requested_games_; ++i) {
        GameSchedule gs;
        if (i < 18) {
            gs = kLatinSequence[i % 9];
        } else if (i == 18) {
            gs = kLatinSequence[0];
        } else {
            gs = kLatinSequence[1];
        }
        gs.human_seat = static_cast<std::int8_t>(i % 2);
        gs.bot_seat = static_cast<std::int8_t>(1 - gs.human_seat);
        schedule_.push_back(gs);
    }
}

GameSchedule ChallengeSession::schedule_game(std::int32_t index) const noexcept {
    if (index < 0 || index >= static_cast<std::int32_t>(schedule_.size())) {
        return {};
    }
    return schedule_[static_cast<std::size_t>(index)];
}

bool ChallengeSession::start_game(std::int32_t index) {
    if (!valid_) return false;
    if (index < 0 || index >= requested_games_) return false;
    if (index != current_game_index_ + 1) return false;
    current_game_index_ = index;
    return true;
}

void ChallengeSession::record_game_result(const GameEvidence& evidence) {
    if (!valid_) return;
    if (evidence.game_index != current_game_index_) {
        return;
    }
    // Reject duplicate results and evidence whose seats or characters
    // disagree with the pre-committed schedule.
    if (static_cast<std::int32_t>(games_.size()) != evidence.game_index) {
        return;
    }
    const GameSchedule expected = schedule_game(evidence.game_index);
    if (evidence.human_seat != expected.human_seat ||
        evidence.bot_seat != expected.bot_seat ||
        evidence.characters != expected.characters) {
        return;
    }
    games_.push_back(evidence);
    append_game_line(evidence);
    flush();
}

void ChallengeSession::append_game_line(const GameEvidence& evidence) {
    std::filesystem::path path = log_dir_ / "games.jsonl";
    std::ofstream file(path, std::ios::app | std::ios::binary);
    if (!file) return;

    std::ostringstream oss;
    oss << "{\"game_index\":" << evidence.game_index
        << ",\"characters\":[\"" << character_name(evidence.characters[0]) << "\",\""
        << character_name(evidence.characters[1]) << "\"]"
        << ",\"human_seat\":" << static_cast<int>(evidence.human_seat)
        << ",\"bot_seat\":" << static_cast<int>(evidence.bot_seat)
        << ",\"winner\":" << static_cast<int>(evidence.winner)
        << ",\"human_score\":" << static_cast<int>(evidence.human_score)
        << ",\"bot_score\":" << static_cast<int>(evidence.bot_score)
        << ",\"final_state_hash\":\"" << json_escape(evidence.final_state_hash) << "\""
        << ",\"replay_path\":\"" << json_escape(evidence.replay_path) << "\""
        << ",\"policy_mode\":\"" << json_escape(evidence.policy_mode) << "\""
        << ",\"network_decision_count\":" << evidence.network_decision_count
        << ",\"non_certified_preview\":" << (evidence.non_certified_preview ? "true" : "false")
        << "}\n";
    file << oss.str();
    file.flush();
}

void ChallengeSession::flush() {
    // games.jsonl is flushed on every append by closing the file.
}

void ChallengeSession::write_summary(ChallengeStatus status) {
    if (!valid_) return;
    SessionSummary s = summary();
    s.status = status;

    std::ostringstream oss;
    oss << "{\n"
        << "  \"total_games\": " << s.total_games << ",\n"
        << "  \"completed_games\": " << s.completed_games << ",\n"
        << "  \"human_wins\": " << s.human_wins << ",\n"
        << "  \"bot_wins\": " << s.bot_wins << ",\n"
        << "  \"human_seat_0_count\": " << s.human_seat_0_count << ",\n"
        << "  \"human_seat_1_count\": " << s.human_seat_1_count << ",\n"
        << "  \"gate_passed\": " << (s.gate_passed ? "true" : "false") << ",\n"
        << "  \"status\": \"" << status_string(s.status) << "\"\n"
        << "}\n";

    std::filesystem::path tmp = log_dir_ / "summary.json.tmp";
    std::filesystem::path final = log_dir_ / "summary.json";
    {
        std::ofstream file(tmp, std::ios::binary);
        if (file) {
            file << oss.str();
            file.flush();
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, final, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
    }
}

void ChallengeSession::write_session_json() {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"checkpoint_id\": \"" << json_escape(policy_.checkpoint_id) << "\",\n"
        << "  \"model_version\": \"" << json_escape(policy_.model_version) << "\",\n"
        << "  \"bundle_sha256\": \"" << json_escape(policy_.bundle_sha) << "\",\n"
        << "  \"manifest_sha256\": \"" << json_escape(policy_.manifest_sha) << "\",\n"
        << "  \"certification_sha\": \"" << json_escape(policy_.certification_sha) << "\",\n"
        << "  \"dll_sha\": \"\"\n"
        << "}\n";

    std::ofstream file(log_dir_ / "session.json", std::ios::binary);
    if (file) {
        file << oss.str();
        file.flush();
    }
}

SessionSummary ChallengeSession::summary() const noexcept {
    SessionSummary s;
    s.total_games = requested_games_;
    s.bundle_sha = policy_.bundle_sha;
    s.manifest_sha = policy_.manifest_sha;
    s.checkpoint_id = policy_.checkpoint_id;
    s.certification_sha = policy_.certification_sha;
    s.dll_sha = "";
    recompute_summary(s);
    return s;
}

void ChallengeSession::recompute_summary(SessionSummary& s) const {
    s.completed_games = static_cast<std::int32_t>(games_.size());
    s.human_wins = 0;
    s.bot_wins = 0;
    s.human_seat_0_count = 0;
    s.human_seat_1_count = 0;
    for (const auto& g : games_) {
        if (g.human_seat == 0) ++s.human_seat_0_count;
        if (g.human_seat == 1) ++s.human_seat_1_count;
        if (g.winner == g.human_seat) ++s.human_wins;
        if (g.winner == g.bot_seat) ++s.bot_wins;
    }
    s.gate_passed = (s.bot_wins >= 14 && s.completed_games == 20 &&
                     s.human_seat_0_count == 10 && s.human_seat_1_count == 10);
}

bool ChallengeSession::gate_passed() const noexcept {
    SessionSummary s;
    recompute_summary(s);
    return s.gate_passed;
}

bool ChallengeSession::can_reset() const noexcept {
    // Can reset if session is valid and not yet complete
    return valid_ && current_game_index_ < requested_games_;
}

bool ChallengeSession::can_discard() const noexcept {
    // Can discard if session is valid and has started at least one game
    return valid_ && current_game_index_ >= 0;
}

void ChallengeSession::reset_session() {
    if (!can_reset()) return;

    current_game_index_ = -1;
    games_.clear();

    // Clear existing evidence files
    std::error_code ec;
    std::filesystem::remove(log_dir_ / "games.jsonl", ec);
    std::filesystem::remove(log_dir_ / "summary.json", ec);

    // Write fresh session.json
    write_session_json();
}

void ChallengeSession::discard_session() {
    if (!can_discard()) return;

    // Write partial summary before discarding
    write_summary(ChallengeStatus::Partial);

    // Mark session as invalid to prevent further operations
    valid_ = false;
    error_ = "Session discarded";
}

const char* ChallengeSession::status_string(ChallengeStatus status) noexcept {
    switch (status) {
        case ChallengeStatus::Active: return "Active";
        case ChallengeStatus::Partial: return "Partial";
        case ChallengeStatus::Complete: return "Complete";
        case ChallengeStatus::Pass: return "Pass";
        case ChallengeStatus::Fail: return "Fail";
    }
    return "Active";
}

std::string ChallengeSession::json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

}  // namespace pulse
