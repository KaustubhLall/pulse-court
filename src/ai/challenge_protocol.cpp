#include "challenge_protocol.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <fileapi.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace pulse::challenge {

namespace {

// Character to string
const char* character_to_string(Character c) {
    switch (c) {
        case Character::Kite: return "Kite";
        case Character::Vale: return "Vale";
        case Character::Bastion: return "Bastion";
        default: return "Unknown";
    }
}

// String to character
Character string_to_character(const std::string& s) {
    if (s == "Kite") return Character::Kite;
    if (s == "Vale") return Character::Vale;
    if (s == "Bastion") return Character::Bastion;
    return Character::Kite;  // fallback
}

// SHA-256 implementation (FIPS 180-4)
void sha256(const unsigned char* data, std::size_t len, unsigned char* out_hash) noexcept {
    static const std::uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
    };
    std::uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19,
    };
    auto rotr = [](std::uint32_t x, int n) -> std::uint32_t {
        return (x >> n) | (x << (32 - n));
    };
    std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8;
    std::vector<unsigned char> msg(data, data + len);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<unsigned char>((bit_len >> (8 * i)) & 0xFF));
    }
    for (std::size_t blk = 0; blk < msg.size(); blk += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[blk + 4*i]) << 24) |
                   (static_cast<std::uint32_t>(msg[blk + 4*i + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[blk + 4*i + 2]) << 8) |
                   (static_cast<std::uint32_t>(msg[blk + 4*i + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            std::uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t temp1 = hh + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = S0 + maj;
            hh=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    for (int i = 0; i < 8; ++i) {
        out_hash[4*i]   = (h[i] >> 24) & 0xFF;
        out_hash[4*i+1] = (h[i] >> 16) & 0xFF;
        out_hash[4*i+2] = (h[i] >> 8) & 0xFF;
        out_hash[4*i+3] = h[i] & 0xFF;
    }
}

std::string sha256_hex(const unsigned char* data, std::size_t len) {
    unsigned char hash[32];
    sha256(data, len, hash);
    static const char hex[] = "0123456789abcdef";
    std::string s(64, '0');
    for (int i = 0; i < 32; ++i) {
        s[2 * i] = hex[(hash[i] >> 4) & 0xF];
        s[2 * i + 1] = hex[hash[i] & 0xF];
    }
    return s;
}

std::string sha256_hex_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        return "";
    }
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    return sha256_hex(data.data(), data.size());
}

} // namespace

ChallengeSession::ChallengeSession(const std::string& log_dir)
    : log_dir_(log_dir),
      current_game_index_(0),
      initialized_(false),
      integrity_violated_(false) {
    // Set default total_games to 20
    summary_.total_games = 20;
    // Other fields already have default values in the struct
}

void ChallengeSession::generate_schedule() {
    schedule_.clear();
    
    // Nine-pair Latin sequence for indices 0..17 (used twice)
    const std::array<std::pair<Character, Character>, 9> pairs = {
        std::make_pair(Character::Kite, Character::Kite),
        std::make_pair(Character::Vale, Character::Vale),
        std::make_pair(Character::Bastion, Character::Bastion),
        std::make_pair(Character::Kite, Character::Vale),
        std::make_pair(Character::Vale, Character::Bastion),
        std::make_pair(Character::Bastion, Character::Kite),
        std::make_pair(Character::Kite, Character::Bastion),
        std::make_pair(Character::Vale, Character::Kite),
        std::make_pair(Character::Bastion, Character::Vale)
    };
    
    int game_count = summary_.total_games;
    
    for (int i = 0; i < game_count; ++i) {
        GameSchedule sched;
        sched.game_index = i;
        sched.human_physical_seat = i % 2;  // strict alternation
        sched.bot_seat = 1 - sched.human_physical_seat;
        
        if (i < 18) {
            // Use nine-pair sequence twice
            int pair_idx = i % 9;
            sched.human_char = pairs[pair_idx].first;
            sched.bot_char = pairs[pair_idx].second;
        } else if (i == 18) {
            // Index 18 repeats (Kite,Kite)
            sched.human_char = Character::Kite;
            sched.bot_char = Character::Kite;
        } else {
            // Index 19+ repeats (Vale,Vale)
            sched.human_char = Character::Vale;
            sched.bot_char = Character::Vale;
        }
        
        schedule_.push_back(sched);
    }
}

std::string ChallengeSession::get_timestamp_utc() {
    time_t now = time(nullptr);
    struct tm tm_utc;
#ifdef _WIN32
    gmtime_s(&tm_utc, &now);
#else
    gmtime_r(&now, &tm_utc);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return std::string(buf);
}

std::string ChallengeSession::compute_simple_hash(const std::string& data) {
    // Compute SHA-256 for simulator/config - cryptographic hash for identification
    return sha256_hex(reinterpret_cast<const unsigned char*>(data.c_str()), data.size());
}

std::string ChallengeSession::json_escape(const std::string& s) {
    std::string result;
    result.reserve(s.size() * 2);  // Reserve space for potential expansion
    
    for (char c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        switch (uc) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                // Escape C0 control characters (0x00-0x1F) except those already handled
                if (uc < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(uc));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }
    
    return result;
}

// Convert 64-bit hash to hex string (lowercase, no prefix)
static std::string uint64_to_hex(uint64_t value) {
    char buf[17];  // 16 hex digits + null terminator
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buf);
}

bool ChallengeSession::initialize(const pulse::PolicyProvenance& provenance, bool competitive_mode, int total_games_override) {
    if (initialized_) {
        return false;  // already initialized
    }
    
    // Store provenance snapshot
    provenance_ = provenance;
    
    // Validate provenance is populated and has valid SHA-256 format
    if (!provenance_.is_populated()) {
        return false;  // invalid provenance
    }
    
    if (!provenance_.is_valid()) {
        return false;  // invalid SHA-256 format
    }
    
    summary_.competitive_gate_applicable = competitive_mode;
    
    // Override total_games if specified
    if (total_games_override > 0) {
        summary_.total_games = total_games_override;
    }
    
    // Check if log directory exists - it must not exist for new session
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(log_dir_.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;  // directory already exists
    }
    // Create directory
    if (!CreateDirectoryA(log_dir_.c_str(), nullptr)) {
        return false;  // failed to create
    }
#else
    struct stat st;
    if (stat(log_dir_.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return false;  // directory already exists
    }
    mkdir(log_dir_.c_str(), 0755);
#endif
    
    // Generate frozen schedule (uses current total_games value)
    generate_schedule();
    
    // Compute schedule identity (SHA-256 of schedule JSON representation)
    std::string schedule_json;
    schedule_json += "[";
    for (size_t i = 0; i < schedule_.size(); ++i) {
        const auto& sched = schedule_[i];
        schedule_json += "{";
        schedule_json += "\"game_index\":" + std::to_string(sched.game_index) + ",";
        schedule_json += "\"human_physical_seat\":" + std::to_string(sched.human_physical_seat) + ",";
        schedule_json += "\"bot_seat\":" + std::to_string(sched.bot_seat) + ",";
        schedule_json += "\"human_char\":\"" + std::string(character_to_string(sched.human_char)) + "\",";
        schedule_json += "\"bot_char\":\"" + std::string(character_to_string(sched.bot_char)) + "\"";
        schedule_json += "}";
        if (i < schedule_.size() - 1) schedule_json += ",";
    }
    schedule_json += "]";
    summary_.schedule_identity = sha256_hex(
        reinterpret_cast<const unsigned char*>(schedule_json.c_str()),
        schedule_json.size()
    );
    
    // Populate summary from provenance snapshot
    summary_.bundle_sha256 = provenance_.bundle_sha256;
    summary_.manifest_sha256 = provenance_.manifest_sha256;
    summary_.checkpoint_sha = provenance_.checkpoint_sha256;
    summary_.certification_sha = provenance_.certification_sha256;
    summary_.dll_sha = provenance_.dll_sha256;
    summary_.config_hash = provenance_.config_sha256;
    summary_.ruleset_hash = provenance_.ruleset_sha256;
    summary_.simulator_hash = provenance_.simulator_sha256;
    
    // Validate required provenance fields for competitive mode
    if (competitive_mode) {
        if (provenance_.config_sha256.empty() || 
            provenance_.ruleset_sha256.empty() || 
            provenance_.simulator_sha256.empty()) {
            return false;  // Missing competitive mode required fields
        }
    }
    
    // Set timestamps
    summary_.start_timestamp_utc = get_timestamp_utc();
    
    // Write session JSON (before game 1)
    if (!write_session_json()) {
        return false;
    }
    
    // Write initial summary
    if (!write_summary_atomic()) {
        return false;
    }
    
    initialized_ = true;
    return true;
}

bool ChallengeSession::write_session_json() {
    std::string path = log_dir_ + "/challenge_session.json";
    std::string temp_path = log_dir_ + "/challenge_session.json.tmp";
    
    std::ofstream f(temp_path);
    if (!f.is_open()) {
        return false;
    }
    
    f << "{\n";
    f << "  \"total_games\": " << summary_.total_games << ",\n";
    f << "  \"competitive_gate_applicable\": " << (summary_.competitive_gate_applicable ? "true" : "false") << ",\n";
    f << "  \"start_timestamp_utc\": \"" << json_escape(summary_.start_timestamp_utc) << "\",\n";
    f << "  \"bundle_sha256\": \"" << json_escape(summary_.bundle_sha256) << "\",\n";
    f << "  \"manifest_sha256\": \"" << json_escape(summary_.manifest_sha256) << "\",\n";
    f << "  \"checkpoint_sha\": \"" << json_escape(summary_.checkpoint_sha) << "\",\n";
    f << "  \"certification_sha\": \"" << json_escape(summary_.certification_sha) << "\",\n";
    f << "  \"dll_sha\": \"" << json_escape(summary_.dll_sha) << "\",\n";
    f << "  \"ruleset_hash\": \"" << json_escape(summary_.ruleset_hash) << "\",\n";
    f << "  \"simulator_hash\": \"" << json_escape(summary_.simulator_hash) << "\",\n";
    f << "  \"config_hash\": \"" << json_escape(summary_.config_hash) << "\",\n";
    f << "  \"schedule\": [\n";
    
    for (size_t i = 0; i < schedule_.size() && i < static_cast<size_t>(summary_.total_games); ++i) {
        const auto& sched = schedule_[i];
        f << "    {\n";
        f << "      \"game_index\": " << sched.game_index << ",\n";
        f << "      \"human_physical_seat\": " << sched.human_physical_seat << ",\n";
        f << "      \"bot_seat\": " << sched.bot_seat << ",\n";
        f << "      \"human_char\": \"" << character_to_string(sched.human_char) << "\",\n";
        f << "      \"bot_char\": \"" << character_to_string(sched.bot_char) << "\"\n";
        f << "    }" << (i < static_cast<size_t>(summary_.total_games) - 1 && i < schedule_.size() - 1 ? "," : "") << "\n";
    }
    
    f << "  ]\n";
    f << "}\n";
    
    if (!f.flush()) {
        f.close();
        return false;
    }
    
    f.close();
    
    // Atomic replace with write-through
#ifdef _WIN32
    if (!MoveFileExA(temp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return false;
    }
#else
    if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
        return false;
    }
#endif
    
    return true;
}

GameSchedule ChallengeSession::get_schedule(int game_index) const {
    if (game_index >= 0 && game_index < static_cast<int>(schedule_.size())) {
        return schedule_[game_index];
    }
    return GameSchedule{};  // invalid
}

bool ChallengeSession::validate_evidence_against_schedule(const GameEvidence& evidence) const {
    if (evidence.game_index < 0 || evidence.game_index >= static_cast<int>(schedule_.size())) {
        return false;
    }
    
    const auto& sched = schedule_[evidence.game_index];
    
    // Validate exact schedule match
    if (evidence.human_seat != sched.human_physical_seat) return false;
    if (evidence.bot_seat != sched.bot_seat) return false;
    if (evidence.human_char != sched.human_char) return false;
    if (evidence.bot_char != sched.bot_char) return false;
    
    return true;
}

bool ChallengeSession::validate_evidence_semantics(const GameEvidence& evidence) const {
    // Competitive mode requires exactly 20 games
    if (summary_.competitive_gate_applicable && summary_.total_games != 20) {
        return false;
    }
    
    // Validate completed state consistency
    if (evidence.completed) {
        // Completed games cannot use "incomplete" termination
        if (evidence.termination_reason == "incomplete") {
            return false;
        }
        
        // Completed games must have a valid termination reason
        if (evidence.termination_reason != "goal" && 
            evidence.termination_reason != "timeout_draw") {
            return false;
        }
        
        // Completed games must have non-negative scores
        if (evidence.score_0 < 0 || evidence.score_1 < 0) {
            return false;
        }
        
        // Score-to-one: if completed by goal, one score must be exactly 1 and the other 0
        if (evidence.termination_reason == "goal") {
            if (!((evidence.score_0 == 1 && evidence.score_1 == 0) || 
                  (evidence.score_0 == 0 && evidence.score_1 == 1))) {
                return false;
            }
        }
        
        // Timeout draw: both scores must be 0 AND ticks must be exactly 12000 AND winner must be -1
        if (evidence.termination_reason == "timeout_draw") {
            if (evidence.score_0 != 0 || evidence.score_1 != 0) {
                return false;
            }
            if (evidence.ticks != 12000) {
                return false;  // Must be exact tick limit for timeout draw
            }
            if (evidence.winner != -1) {
                return false;  // Timeout draw must have draw winner
            }
        }
        
        // Winner must agree with scores (strict validation)
        // Reject caller-supplied winner when scores contradict
        if (evidence.score_0 > evidence.score_1) {
            // Seat 0 has higher score, winner must be 0
            if (evidence.winner != 0) {
                return false;
            }
        } else if (evidence.score_1 > evidence.score_0) {
            // Seat 1 has higher score, winner must be 1
            if (evidence.winner != 1) {
                return false;
            }
        } else {
            // Scores are equal, winner must be -1 (draw)
            if (evidence.winner != -1) {
                return false;
            }
        }
        
        // Additional cross-check: winner must match seat mapping
        // This is redundant with the above but provides defense-in-depth
        if (evidence.winner == evidence.human_seat) {
            // Human won - scores must reflect this
            if (evidence.human_seat == 0 && evidence.score_0 <= evidence.score_1) {
                return false;
            }
            if (evidence.human_seat == 1 && evidence.score_1 <= evidence.score_0) {
                return false;
            }
        }
        if (evidence.winner == evidence.bot_seat) {
            // Bot won - scores must reflect this
            if (evidence.bot_seat == 0 && evidence.score_0 <= evidence.score_1) {
                return false;
            }
            if (evidence.bot_seat == 1 && evidence.score_1 <= evidence.score_0) {
                return false;
            }
        }
    } else {
        // Incomplete games should not have a goal termination
        if (evidence.termination_reason == "goal") {
            return false;
        }
    }
    
    // Tick bounds validation (strict: must be within [1, 12000], reject 0)
    if (evidence.ticks <= 0 || evidence.ticks > 12000) {
        return false;
    }
    
    // Game index bounds
    if (evidence.game_index < 0 || evidence.game_index >= summary_.total_games) {
        return false;
    }
    
    // Reject invalid states: state_hash must be non-zero for completed games
    if (evidence.completed && evidence.state_hash == 0) {
        return false;
    }
    
    // Reject integrity_flags = true for completed games (failure can never make gate pass)
    if (evidence.completed && evidence.integrity_flags) {
        return false;
    }
    
    return true;
}

bool ChallengeSession::append_game_jsonl(const GameEvidence& evidence) {
    std::string path = log_dir_ + "/challenge_games.jsonl";
    
#ifdef _WIN32
    FILE* f = nullptr;
    // Use "a" mode for append-only (no overwrite)
    if (fopen_s(&f, path.c_str(), "a") != 0) {
        return false;
    }
#else
    // Use "a" mode for append-only (no overwrite)
    FILE* f = fopen(path.c_str(), "a");
    if (!f) {
        return false;
    }
#endif
    
    std::string escaped_timestamp = json_escape(evidence.timestamp_utc);
    std::string escaped_termination = json_escape(evidence.termination_reason);
    std::string state_hash_hex = uint64_to_hex(evidence.state_hash);
    int write_result = fprintf(f, "{\"game_index\":%d,\"human_seat\":%d,\"bot_seat\":%d,"
               "\"human_char\":\"%s\",\"bot_char\":\"%s\",\"winner\":%d,"
               "\"score_0\":%d,\"score_1\":%d,\"ticks\":%d,\"state_hash\":\"%s\","
               "\"timestamp_utc\":\"%s\",\"completed\":%s,\"integrity_flags\":%s,"
               "\"termination_reason\":\"%s\"}\n",
            evidence.game_index,
            evidence.human_seat,
            evidence.bot_seat,
            character_to_string(evidence.human_char),
            character_to_string(evidence.bot_char),
            evidence.winner,
            evidence.score_0,
            evidence.score_1,
            evidence.ticks,
            state_hash_hex.c_str(),
            escaped_timestamp.c_str(),
            evidence.completed ? "true" : "false",
            evidence.integrity_flags ? "true" : "false",
            escaped_termination.c_str());
    
    if (write_result < 0) {
        fclose(f);
        return false;
    }
    
    // Strict flush to OS: fflush before FlushFileBuffers
    if (fflush(f) != 0) {
        fclose(f);
        return false;
    }
    
#ifdef _WIN32
    int fd = _fileno(f);
    if (fd == -1) {
        fclose(f);
        return false;
    }
    HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (handle == INVALID_HANDLE_VALUE) {
        fclose(f);
        return false;
    }
    if (!FlushFileBuffers(handle)) {
        fclose(f);
        return false;
    }
#endif
    
    if (fclose(f) != 0) {
        return false;
    }
    
    return true;
}

bool ChallengeSession::record_game(const GameEvidence& evidence) {
    if (!initialized_) {
        return false;
    }
    
    // Validate exact next index
    if (evidence.game_index != current_game_index_) {
        return false;
    }
    
    // Validate against schedule
    if (!validate_evidence_against_schedule(evidence)) {
        return false;
    }
    
    // Validate evidence semantics
    if (!validate_evidence_semantics(evidence)) {
        return false;
    }
    
    // Check for duplicate game_index
    for (const auto& ev : evidence_) {
        if (ev.game_index == evidence.game_index) {
            return false;
        }
    }
    
    GameEvidence ev = evidence;  // Make a copy to set timestamp
    ev.timestamp_utc = get_timestamp_utc();  // Set timestamp here
    
    // Append to JSONL FIRST (before updating in-memory state)
    if (!append_game_jsonl(ev)) {
        return false;  // I/O error: do not mutate state
    }
    
    // Only update in-memory state after successful append
    evidence_.push_back(ev);
    
    // Update summary
    if (ev.completed) {
        summary_.completed_games++;
        
        if (ev.winner == ev.bot_seat) {
            summary_.bot_wins++;
            // Track bot wins by seat
            if (ev.bot_seat == 0) {
                summary_.bot_seat_0_wins++;
            } else {
                summary_.bot_seat_1_wins++;
            }
        } else if (ev.winner == ev.human_seat) {
            summary_.human_wins++;
            // Track human wins by seat
            if (ev.human_seat == 0) {
                summary_.human_seat_0_wins++;
            } else {
                summary_.human_seat_1_wins++;
            }
        } else {
            summary_.draws++;
        }
        
        if (ev.human_seat == 0) {
            summary_.human_seat_0_count++;
        } else {
            summary_.human_seat_1_count++;
        }
        
        // Track matchup outcomes (from human perspective)
        std::string matchup_key = std::string(character_to_string(ev.human_char)) + "_" + 
                                   std::string(character_to_string(ev.bot_char));
        auto& matchup = summary_.matchup_outcomes[matchup_key];
        if (ev.winner == ev.human_seat) {
            std::get<0>(matchup)++;  // human win
        } else if (ev.winner == ev.bot_seat) {
            std::get<1>(matchup)++;  // human loss (bot win)
        } else {
            std::get<2>(matchup)++;  // draw
        }
    }
    
    // Aggregate integrity: any violation fails the gate
    // Integrity flags include restart attempts, protocol violations, and any tampering
    if (ev.integrity_flags) {
        summary_.results_integrity = false;
    }
    
    // Schedule integrity: any evidence that doesn't match the frozen schedule fails the gate
    // This is already checked in validate_evidence_against_schedule before record_game is called
    // If we reach here, schedule integrity is maintained for this game
    
    // Provenance integrity: any missing or invalid provenance fails the gate
    // This is checked during initialization - if we got here, provenance was valid at init time
    
    current_game_index_++;
    
    // Update completion state after each write
    if (summary_.completed_games == summary_.total_games) {
        summary_.completion_state = "COMPLETE";
    } else if (summary_.completed_games > 0) {
        summary_.completion_state = "PARTIAL";
    } else {
        summary_.completion_state = "ACTIVE";
    }
    
    // Write updated summary (atomic replace)
    return write_summary_atomic();
}

bool ChallengeSession::write_summary_atomic() {
    std::string path = log_dir_ + "/challenge_summary.json";
    std::string temp_path = log_dir_ + "/challenge_summary.json.tmp";
    
    std::ofstream f(temp_path);
    if (!f.is_open()) {
        return false;
    }
    
    summary_.end_timestamp_utc = get_timestamp_utc();
    summary_.gate_passed = calculate_gate();
    
    // completion_state is now maintained in record_game(), use it directly
    const char* completion_state = summary_.completion_state.c_str();
    
    f << "{\n";
    f << "  \"total_games\": " << summary_.total_games << ",\n";
    f << "  \"completed_games\": " << summary_.completed_games << ",\n";
    f << "  \"bot_wins\": " << summary_.bot_wins << ",\n";
    f << "  \"human_wins\": " << summary_.human_wins << ",\n";
    f << "  \"draws\": " << summary_.draws << ",\n";
    f << "  \"human_seat_0_count\": " << summary_.human_seat_0_count << ",\n";
    f << "  \"human_seat_1_count\": " << summary_.human_seat_1_count << ",\n";
    f << "  \"gate_passed\": " << (summary_.gate_passed ? "true" : "false") << ",\n";
    f << "  \"competitive_gate_applicable\": " << (summary_.competitive_gate_applicable ? "true" : "false") << ",\n";
    f << "  \"completion_state\": \"" << json_escape(completion_state) << "\",\n";
    f << "  \"start_timestamp_utc\": \"" << json_escape(summary_.start_timestamp_utc) << "\",\n";
    f << "  \"end_timestamp_utc\": \"" << json_escape(summary_.end_timestamp_utc) << "\",\n";
    f << "  \"bundle_sha256\": \"" << json_escape(summary_.bundle_sha256) << "\",\n";
    f << "  \"manifest_sha256\": \"" << json_escape(summary_.manifest_sha256) << "\",\n";
    f << "  \"checkpoint_sha\": \"" << json_escape(summary_.checkpoint_sha) << "\",\n";
    f << "  \"certification_sha\": \"" << json_escape(summary_.certification_sha) << "\",\n";
    f << "  \"dll_sha\": \"" << json_escape(summary_.dll_sha) << "\",\n";
    f << "  \"ruleset_hash\": \"" << json_escape(summary_.ruleset_hash) << "\",\n";
    f << "  \"simulator_hash\": \"" << json_escape(summary_.simulator_hash) << "\",\n";
    f << "  \"config_hash\": \"" << json_escape(summary_.config_hash) << "\",\n";
    f << "  \"schedule_integrity\": " << (summary_.schedule_integrity ? "true" : "false") << ",\n";
    f << "  \"results_integrity\": " << (summary_.results_integrity ? "true" : "false") << ",\n";
    f << "  \"provenance_integrity\": " << (summary_.provenance_integrity ? "true" : "false") << ",\n";
    f << "  \"schedule_identity\": \"" << json_escape(summary_.schedule_identity) << "\",\n";
    f << "  \"bot_seat_0_wins\": " << summary_.bot_seat_0_wins << ",\n";
    f << "  \"bot_seat_1_wins\": " << summary_.bot_seat_1_wins << ",\n";
    f << "  \"human_seat_0_wins\": " << summary_.human_seat_0_wins << ",\n";
    f << "  \"human_seat_1_wins\": " << summary_.human_seat_1_wins << ",\n";
    f << "  \"matchup_outcomes\": {\n";
    
    bool first_matchup = true;
    for (const auto& [key, outcomes] : summary_.matchup_outcomes) {
        if (!first_matchup) f << ",\n";
        f << "    \"" << json_escape(key) << "\": {";
        f << "\"wins\":" << std::get<0>(outcomes) << ",";
        f << "\"losses\":" << std::get<1>(outcomes) << ",";
        f << "\"draws\":" << std::get<2>(outcomes) << "}";
        first_matchup = false;
    }
    
    f << "\n  },\n";
    f << "  \"schedule\": [\n";
    
    for (size_t i = 0; i < schedule_.size() && i < static_cast<size_t>(summary_.total_games); ++i) {
        const auto& sched = schedule_[i];
        f << "    {\n";
        f << "      \"game_index\": " << sched.game_index << ",\n";
        f << "      \"human_physical_seat\": " << sched.human_physical_seat << ",\n";
        f << "      \"bot_seat\": " << sched.bot_seat << ",\n";
        f << "      \"human_char\": \"" << character_to_string(sched.human_char) << "\",\n";
        f << "      \"bot_char\": \"" << character_to_string(sched.bot_char) << "\"\n";
        f << "    }" << (i < static_cast<size_t>(summary_.total_games) - 1 && i < schedule_.size() - 1 ? "," : "") << "\n";
    }
    
    f << "  ]\n";
    f << "}\n";
    
    if (!f.flush()) {
        f.close();
        return false;
    }
    
    f.close();
    
    // Atomic replace with write-through
#ifdef _WIN32
    if (!MoveFileExA(temp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return false;
    }
#else
    if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
        return false;
    }
#endif
    
    return true;
}

bool ChallengeSession::write_summary() {
    return write_summary_atomic();
}

bool ChallengeSession::calculate_gate() const {
    // If competitive gate is not applicable (e.g., bounded smoke), gate never passes
    if (!summary_.competitive_gate_applicable) {
        return false;
    }
    
    // Validate matchup sums match overall W/L/D counts
    int matchup_wins = 0, matchup_losses = 0, matchup_draws = 0;
    for (const auto& [key, outcomes] : summary_.matchup_outcomes) {
        matchup_wins += std::get<0>(outcomes);
        matchup_losses += std::get<1>(outcomes);
        matchup_draws += std::get<2>(outcomes);
    }
    
    if (matchup_wins != summary_.human_wins || 
        matchup_losses != summary_.bot_wins || 
        matchup_draws != summary_.draws) {
        return false;  // Matchup sums don't validate
    }
    
    // Strict gate: wins>=14, completion20, exact seat counts, exact schedule/result identity
    return summary_.bot_wins >= 14 &&
           summary_.completed_games == summary_.total_games &&
           summary_.human_seat_0_count == 10 &&
           summary_.human_seat_1_count == 10 &&
           summary_.schedule_integrity &&
           summary_.results_integrity &&
           summary_.provenance_integrity;
}

int ChallengeSession::current_game_index() const {
    return current_game_index_;
}

bool ChallengeSession::is_complete() const {
    return current_game_index_ >= summary_.total_games;
}

void ChallengeSession::mark_integrity_violation(const std::string& reason) {
    integrity_violated_ = true;
    summary_.results_integrity = false;
    // Log the reason (could write to a separate integrity log file in the future)
    std::cerr << "Integrity violation: " << reason << std::endl;
}

} // namespace pulse::challenge
