// pulse_dev_bundle: TEST/DEV ONLY -- NON-CERTIFIED synthetic policy bundle
// generator.
//
// Writes a deterministic, non-trained ".pzpb" bundle (patterned tiny
// non-zero weights) plus a matching ".pzg" golden file whose expected
// actions/logits are the real forward-pass output of that bundle. Useful
// for local smoke-testing of tooling that consumes the bundle/golden file
// formats without needing a real trained checkpoint.
//
// These artifacts carry NO provenance/certification and will NEVER pass
// Policy::load()'s strict three-artifact manifest validation. Only
// Policy::load_preview() / load_from_bytes() (the dev/test-only loader
// path) can load the ".pzpb" bundle produced here.
//
// Usage: pulse_dev_bundle <out-path-prefix>
//   Writes <prefix>.pzpb and <prefix>.pzg. Refuses to overwrite existing
//   files (exits nonzero without touching output paths).
//
// Exit codes: 0 = success, 1 = usage/argument error, 2 = output file
// already exists, 3 = internal generation/verification error.

#include "pulse_ai.hpp"
#include "pulse_policy.hpp"
#include "pulse_policy_adapter.hpp"
#include "pulse_policy_devgen.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>

namespace {

bool file_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

bool write_file(const std::string& path, const std::vector<unsigned char>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: pulse_dev_bundle <out-path-prefix>\n");
        std::fprintf(stderr, "  writes <prefix>.pzpb and <prefix>.pzg (TEST/DEV ONLY, NON-CERTIFIED)\n");
        std::fprintf(stderr, "  refuses to overwrite existing files\n");
        return 1;
    }
    const std::string prefix = argv[1];
    if (prefix.empty()) {
        std::fprintf(stderr, "error: out-path-prefix must be non-empty\n");
        return 1;
    }
    const std::string bundle_path = prefix + ".pzpb";
    const std::string golden_path = prefix + ".pzg";

    if (file_exists(bundle_path)) {
        std::fprintf(stderr, "error: refusing to overwrite existing file: %s\n", bundle_path.c_str());
        return 2;
    }
    if (file_exists(golden_path)) {
        std::fprintf(stderr, "error: refusing to overwrite existing file: %s\n", golden_path.c_str());
        return 2;
    }

    // Build a deterministic bundle with tiny non-zero patterned weights so
    // different observations can produce different actions.
    auto bundle_bytes = pulse::devgen::build_synthetic_bundle(
        0, pulse::devgen::WeightPattern::Patterned);

    // Load it back (in-memory) so the golden file we emit reflects the
    // bundle's real forward-pass output, not a stub.
    pulse::Policy policy;
    if (!policy.load_from_bytes(bundle_bytes)) {
        std::fprintf(stderr, "internal error: generated bundle failed to self-load\n");
        return 3;
    }

    // Build a handful of golden cases from varied deterministic observations.
    constexpr std::size_t kCaseCount = 8;
    std::vector<pulse::devgen::DevGoldenCase> cases;
    cases.reserve(kCaseCount);
    for (std::size_t i = 0; i < kCaseCount; ++i) {
        pulse::devgen::DevGoldenCase gc;
        gc.seed = 2000001 + i;
        gc.player_index = 0;
        gc.learner_seat = static_cast<std::uint8_t>(i % 2);
        gc.char0 = 0;
        gc.char1 = 0;
        gc.opponent_kind = 1;
        gc.sim_tick = static_cast<std::uint32_t>(i * 10);
        for (std::size_t j = 0; j < gc.observation.size(); ++j) {
            gc.observation[j] = pulse::devgen::pattern_weight(i * 97 + j);
        }
        gc.button_mask = {1, 1, 1, 1};  // all buttons legal

        pulse::PolicyOutput out = policy.forward(gc.observation);
        gc.move_logits = out.move_logits;
        gc.aim_logits = out.aim_logits;
        gc.button_logits = out.button_logits;
        gc.value = out.value;

        auto factors = pulse::ai::greedy_factors_from_logits(
            out.move_logits, out.aim_logits, out.button_logits, gc.button_mask);
        gc.expected_move = static_cast<std::uint8_t>(factors[0]);
        gc.expected_aim = static_cast<std::uint8_t>(factors[1]);
        gc.expected_button = static_cast<std::uint8_t>(factors[2]);

        cases.push_back(gc);
    }

    auto golden_bytes = pulse::devgen::build_synthetic_golden_from_cases(cases);

    // Self-check before writing: load the golden bytes back and verify zero
    // mismatches against the bundle we just built.
    if (!policy.load_golden_from_bytes(golden_bytes)) {
        std::fprintf(stderr, "internal error: generated golden file failed to self-load\n");
        return 3;
    }
    std::size_t mismatches = policy.verify_golden_cases();
    if (mismatches != 0) {
        std::fprintf(stderr, "internal error: %zu/%zu generated golden cases mismatch\n",
                     mismatches, policy.golden_case_count());
        return 3;
    }

    if (!write_file(bundle_path, bundle_bytes)) {
        std::fprintf(stderr, "error: failed to write %s\n", bundle_path.c_str());
        return 3;
    }
    if (!write_file(golden_path, golden_bytes)) {
        std::fprintf(stderr, "error: failed to write %s\n", golden_path.c_str());
        return 3;
    }

    std::printf("wrote %s\n", bundle_path.c_str());
    std::printf("wrote %s\n", golden_path.c_str());
    std::printf("bundle sha256: %s\n", policy.bundle_sha256().c_str());
    std::printf("golden cases: %zu\n", policy.golden_case_count());
    std::fprintf(stderr, "NOTE: TEST/DEV ONLY -- NON-CERTIFIED. Not usable with Policy::load() strict path.\n");

    return 0;
}
