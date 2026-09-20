#include "pulse_replay.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace pulse {

namespace {

constexpr char kReplayMagic[8] = {'P', 'U', 'L', 'S', 'E', 'R', 'P', 'L'};
constexpr std::uint64_t kReplayHeaderBytes = 34;
constexpr std::uint64_t kReplayTickBytes = 18;  // Two 5-byte inputs + 8-byte hash.

void write_u32(std::ofstream& out, std::uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

void write_u64(std::ofstream& out, std::uint64_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

void write_i32(std::ofstream& out, std::int32_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

void write_u8(std::ofstream& out, std::uint8_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
}

bool read_u32(std::ifstream& in, std::uint32_t& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return in.gcount() == sizeof(v);
}

bool read_u64(std::ifstream& in, std::uint64_t& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return in.gcount() == sizeof(v);
}

bool read_i32(std::ifstream& in, std::int32_t& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return in.gcount() == sizeof(v);
}

bool read_u8(std::ifstream& in, std::uint8_t& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    return in.gcount() == sizeof(v);
}

}  // namespace

bool save_replay(const Replay& replay, const std::string& path,
                 std::string& error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "failed to open replay for writing: " + path;
        return false;
    }

    out.write(kReplayMagic, sizeof(kReplayMagic));
    write_u32(out, replay.format_version);
    write_u32(out, replay.ruleset_version);
    write_u8(out, static_cast<std::uint8_t>(replay.characters[0]));
    write_u8(out, static_cast<std::uint8_t>(replay.characters[1]));
    write_i32(out, replay.config.score_to_win);
    write_i32(out, replay.config.regulation_ticks);
    write_i32(out, replay.config.kickoff_ticks);
    write_u32(out, static_cast<std::uint32_t>(replay.ticks.size()));

    for (const auto& t : replay.ticks) {
        for (const auto& in : t.inputs) {
            write_u8(out, static_cast<std::uint8_t>(in.move_x));
            write_u8(out, static_cast<std::uint8_t>(in.move_y));
            write_u8(out, static_cast<std::uint8_t>(in.aim_x));
            write_u8(out, static_cast<std::uint8_t>(in.aim_y));
            write_u8(out, in.buttons);
        }
        write_u64(out, t.expected_hash);
    }

    if (!out) {
        error = "failed to write replay data";
        return false;
    }
    return true;
}

bool load_replay(const std::string& path, Replay& replay, std::string& error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "failed to open replay: " + path;
        return false;
    }

    const auto end = in.tellg();
    if (end < std::streampos(0)) {
        error = "failed to determine replay size";
        return false;
    }
    const auto file_bytes = static_cast<std::uint64_t>(end);
    if (file_bytes < kReplayHeaderBytes) {
        error = "replay header truncated";
        return false;
    }
    in.seekg(0, std::ios::beg);
    char magic[sizeof(kReplayMagic)] = {};
    in.read(magic, sizeof(magic));
    if (in.gcount() != sizeof(magic) ||
        std::memcmp(magic, kReplayMagic, sizeof(magic)) != 0) {
        error = "replay has invalid magic";
        return false;
    }

    std::uint32_t format_version = 0;
    std::uint32_t ruleset_version = 0;
    std::uint8_t left_char = 0;
    std::uint8_t right_char = 0;
    std::int32_t score_to_win = 0;
    std::int32_t regulation_ticks = 0;
    std::int32_t kickoff_ticks = 0;
    std::uint32_t tick_count = 0;

    if (!read_u32(in, format_version) || !read_u32(in, ruleset_version) ||
        !read_u8(in, left_char) || !read_u8(in, right_char) ||
        !read_i32(in, score_to_win) || !read_i32(in, regulation_ticks) ||
        !read_i32(in, kickoff_ticks) || !read_u32(in, tick_count)) {
        error = "replay header truncated";
        return false;
    }

    if (format_version != kReplayFormatVersion) {
        error = "incompatible replay format version";
        return false;
    }
    if (ruleset_version != kRulesetVersion) {
        error = "incompatible ruleset version";
        return false;
    }

    // Widen before multiplying: even UINT32_MAX ticks fit in uint64_t.
    // Validate the complete on-disk extent before allocating from this count.
    const std::uint64_t expected_bytes =
        kReplayHeaderBytes + static_cast<std::uint64_t>(tick_count) * kReplayTickBytes;
    if (file_bytes < expected_bytes) {
        const auto partial_tick_bytes = (file_bytes - kReplayHeaderBytes) % kReplayTickBytes;
        error = partial_tick_bytes < 10 ? "replay frame truncated" : "replay hash truncated";
        return false;
    }
    if (file_bytes > expected_bytes) {
        error = "replay has trailing bytes";
        return false;
    }

    Replay parsed;
    parsed.format_version = format_version;
    parsed.ruleset_version = ruleset_version;
    parsed.characters[0] = static_cast<Character>(left_char);
    parsed.characters[1] = static_cast<Character>(right_char);
    parsed.config.score_to_win = score_to_win;
    parsed.config.regulation_ticks = regulation_ticks;
    parsed.config.kickoff_ticks = kickoff_ticks;
    parsed.ticks.resize(tick_count);

    for (std::uint32_t i = 0; i < tick_count; ++i) {
        for (std::size_t p = 0; p < 2; ++p) {
            std::uint8_t mx, my, ax, ay, bt;
            if (!read_u8(in, mx) || !read_u8(in, my) || !read_u8(in, ax) ||
                !read_u8(in, ay) || !read_u8(in, bt)) {
                error = "replay frame truncated";
                return false;
            }
            auto& fi = parsed.ticks[i].inputs[p];
            fi.move_x = static_cast<std::int8_t>(mx);
            fi.move_y = static_cast<std::int8_t>(my);
            fi.aim_x = static_cast<std::int8_t>(ax);
            fi.aim_y = static_cast<std::int8_t>(ay);
            fi.buttons = bt;
        }
        if (!read_u64(in, parsed.ticks[i].expected_hash)) {
            error = "replay hash truncated";
            return false;
        }
    }

    if (!in) {
        error = "replay file corrupted";
        return false;
    }
    // Also reject growth since tellg(); reads above detect concurrent truncation.
    if (in.peek() != std::char_traits<char>::eof()) {
        error = "replay has trailing bytes";
        return false;
    }
    if (in.bad()) {
        error = "replay file corrupted";
        return false;
    }
    replay = std::move(parsed);
    error.clear();
    return true;
}

ReplayVerification verify_replay(const Replay& replay, MatchConfig config) {
    ReplayVerification v{};
    (void)config;  // replays carry their own match config to avoid silent drift
    Simulation sim(replay.characters, replay.config);

    for (std::uint32_t i = 0; i < replay.ticks.size(); ++i) {
        sim.step(replay.ticks[i].inputs);
        std::uint64_t hash = sim.state_hash();
        if (hash != replay.ticks[i].expected_hash) {
            v.ok = false;
            v.failing_tick = i;
            v.expected_hash = replay.ticks[i].expected_hash;
            v.actual_hash = hash;
            v.message = "hash mismatch at tick " + std::to_string(i);
            return v;
        }
    }

    v.ok = true;
    return v;
}

}  // namespace pulse
