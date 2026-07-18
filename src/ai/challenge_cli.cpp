#include "challenge_cli.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <string>

namespace pulse {

namespace {

std::string narrow(const wchar_t* w) {
    if (!w) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s;
    s.resize(static_cast<std::size_t>(len));
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

bool parse_int(const std::string& s, std::int32_t& out) {
    try {
        std::size_t idx = 0;
        out = std::stoi(s, &idx);
        return idx == s.size();
    } catch (...) {
        return false;
    }
}

}  // namespace

ChallengeViewerArgs parse_challenge_viewer_args(int argc,
                                                 const wchar_t** argv) noexcept {
    ChallengeViewerArgs args;
    bool bundle_set = false;
    bool manifest_set = false;
    bool games_set = false;
    bool log_dir_set = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = narrow(argv[i]);
        if (arg == "--challenge") {
            if (args.challenge) {
                args.error = "duplicate --challenge";
                return args;
            }
            args.challenge = true;
        } else if (arg == "--policy-bundle") {
            if (i + 1 >= argc) {
                args.error = "missing value for --policy-bundle";
                return args;
            }
            if (bundle_set) {
                args.error = "duplicate --policy-bundle";
                return args;
            }
            args.bundle_path = narrow(argv[++i]);
            bundle_set = true;
        } else if (arg == "--policy-manifest") {
            if (i + 1 >= argc) {
                args.error = "missing value for --policy-manifest";
                return args;
            }
            if (manifest_set) {
                args.error = "duplicate --policy-manifest";
                return args;
            }
            args.manifest_path = narrow(argv[++i]);
            manifest_set = true;
        } else if (arg == "--challenge-games") {
            if (i + 1 >= argc) {
                args.error = "missing value for --challenge-games";
                return args;
            }
            if (games_set) {
                args.error = "duplicate --challenge-games";
                return args;
            }
            std::string v = narrow(argv[++i]);
            if (!parse_int(v, args.games) || args.games != 20) {
                args.error = "--challenge-games must be 20";
                return args;
            }
            games_set = true;
        } else if (arg == "--challenge-log-dir") {
            if (i + 1 >= argc) {
                args.error = "missing value for --challenge-log-dir";
                return args;
            }
            if (log_dir_set) {
                args.error = "duplicate --challenge-log-dir";
                return args;
            }
            args.log_dir = narrow(argv[++i]);
            log_dir_set = true;
        } else if (arg == "--setup") {
            args.setup = true;
        } else if (arg == "--gallery") {
            args.gallery = true;
        } else if (arg.empty() || arg[0] != '-') {
            if (args.challenge) {
                args.error = "unexpected positional argument";
                return args;
            }
        } else {
            if (args.challenge) {
                args.error = "unknown argument: " + arg;
                return args;
            }
        }
    }

    if (args.challenge) {
        if (!bundle_set) {
            args.error = "missing --policy-bundle";
            return args;
        }
        if (!manifest_set) {
            args.error = "missing --policy-manifest";
            return args;
        }
        if (!games_set) {
            args.error = "missing --challenge-games";
            return args;
        }
        if (!log_dir_set) {
            args.error = "missing --challenge-log-dir";
            return args;
        }
        if (args.setup || args.gallery) {
            args.error = "--challenge incompatible with --setup/--gallery";
            return args;
        }
    }

    return args;
}

ChallengeHeadlessArgs parse_challenge_headless_args(
    int argc, const char** argv) noexcept {
    ChallengeHeadlessArgs args;
    bool bundle_set = false;
    bool manifest_set = false;
    bool preview_bundle_set = false;
    bool games_set = false;
    bool output_dir_set = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--challenge-smoke") {
            if (args.smoke) {
                args.error = "duplicate --challenge-smoke";
                return args;
            }
            args.smoke = true;
        } else if (arg == "--policy-bundle") {
            if (i + 1 >= argc) {
                args.error = "missing value for --policy-bundle";
                return args;
            }
            if (bundle_set) {
                args.error = "duplicate --policy-bundle";
                return args;
            }
            args.bundle_path = argv[++i];
            bundle_set = true;
        } else if (arg == "--policy-bundle-preview") {
            if (i + 1 >= argc) {
                args.error = "missing value for --policy-bundle-preview";
                return args;
            }
            if (preview_bundle_set) {
                args.error = "duplicate --policy-bundle-preview";
                return args;
            }
            args.preview_bundle_path = argv[++i];
            preview_bundle_set = true;
        } else if (arg == "--policy-manifest") {
            if (i + 1 >= argc) {
                args.error = "missing value for --policy-manifest";
                return args;
            }
            if (manifest_set) {
                args.error = "duplicate --policy-manifest";
                return args;
            }
            args.manifest_path = argv[++i];
            manifest_set = true;
        } else if (arg == "--games") {
            if (i + 1 >= argc) {
                args.error = "missing value for --games";
                return args;
            }
            if (games_set) {
                args.error = "duplicate --games";
                return args;
            }
            std::string v = argv[++i];
            if (!parse_int(v, args.games) || args.games != 6) {
                args.error = "--games must be 6";
                return args;
            }
            games_set = true;
        } else if (arg == "--output-dir") {
            if (i + 1 >= argc) {
                args.error = "missing value for --output-dir";
                return args;
            }
            if (output_dir_set) {
                args.error = "duplicate --output-dir";
                return args;
            }
            args.output_dir = argv[++i];
            output_dir_set = true;
        } else if (arg.empty() || arg[0] != '-') {
            if (args.smoke) {
                args.error = "unexpected positional argument";
                return args;
            }
        } else {
            if (args.smoke) {
                args.error = "unknown argument: " + arg;
                return args;
            }
        }
    }

    if (args.smoke) {
        if (bundle_set && preview_bundle_set) {
            args.error =
                "--policy-bundle and --policy-bundle-preview are mutually "
                "exclusive";
            return args;
        }
        if (!bundle_set && !preview_bundle_set) {
            args.error = "missing --policy-bundle or --policy-bundle-preview";
            return args;
        }
        if (bundle_set && !manifest_set) {
            args.error = "missing --policy-manifest";
            return args;
        }
        // Preview mode loads via the non-strict single-file path and does
        // not require (or use) a manifest; a --policy-manifest alongside
        // --policy-bundle-preview is simply ignored, not an error.
        if (!games_set) {
            args.error = "missing --games";
            return args;
        }
        if (!output_dir_set) {
            args.error = "missing --output-dir";
            return args;
        }
    }

    return args;
}

}  // namespace pulse
