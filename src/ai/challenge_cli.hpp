#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pulse {

struct ChallengeViewerArgs {
    bool challenge = false;
    std::string bundle_path;
    std::string manifest_path;
    std::int32_t games = 0;
    std::string log_dir;
    std::string error;
    bool setup = false;
    bool gallery = false;
};

struct ChallengeHeadlessArgs {
    bool smoke = false;
    std::string bundle_path;
    std::string manifest_path;
    std::int32_t games = 0;
    std::string output_dir;
    std::string error;
};

[[nodiscard]] ChallengeViewerArgs parse_challenge_viewer_args(
    int argc, const wchar_t** argv) noexcept;

[[nodiscard]] ChallengeHeadlessArgs parse_challenge_headless_args(
    int argc, const char** argv) noexcept;

}  // namespace pulse
