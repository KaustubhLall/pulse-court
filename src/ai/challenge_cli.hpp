#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pulse {

struct ChallengeViewerArgs {
    bool challenge = false;
    std::string bundle_path;
    std::string manifest_path;
    // Non-certified preview bundle path, set by --policy-bundle-preview.
    // Mutually exclusive with --policy-bundle/--policy-manifest. Empty
    // means preview mode was not requested.
    std::string preview_bundle_path;
    std::int32_t games = 0;
    std::string log_dir;
    std::string error;
    bool setup = false;
    bool gallery = false;
    bool replay = false;
};

struct ChallengeHeadlessArgs {
    bool smoke = false;
    // Explicit deterministic scripted smoke mode. This must never be inferred
    // from a missing strict bundle path.
    bool scripted_policy = false;
    std::string bundle_path;
    std::string manifest_path;
    // T4: non-certified preview bundle path, set by --policy-bundle-preview.
    // Mutually exclusive with --policy-bundle/bundle_path. Empty means
    // preview mode was not requested.
    std::string preview_bundle_path;
    std::int32_t games = 0;
    std::string output_dir;
    std::string error;
};

[[nodiscard]] ChallengeViewerArgs parse_challenge_viewer_args(
    int argc, const wchar_t** argv) noexcept;

[[nodiscard]] ChallengeHeadlessArgs parse_challenge_headless_args(
    int argc, const char** argv) noexcept;

}  // namespace pulse
