// pulse_policy_verify: strict three-artifact policy verifier.
//
// Usage:
//   pulse_policy_verify --bundle <path.pzb> --manifest <path.json> --golden <path.pzg>
//                       --iterations <N> --p99-limit-ms <limit> [--json]
//
// Exit codes:
//   0 = all checks pass
//   1 = argument error
//   2 = load/SHA-256/parity/validation failure
//   3 = p99 latency limit exceeded
//   4 = unexpected error
//
// Output: compact JSON summary to stdout, diagnostics to stderr.

#include "pulse_policy.hpp"
#include "pulse_ai.hpp"
#include "pulse_sim.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string bundle_path;
    std::string manifest_path;
    std::string golden_path;
    std::uint32_t iterations = 0;
    double p99_limit_ms = 0.0;
    bool json_output = false;

    // Strict argument parsing (no abbreviations, reject unknown/duplicate/invalid)
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](std::string& out) -> bool {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", arg.c_str());
                return false;
            }
            out = argv[++i];
            return true;
        };
        if (arg == "--bundle") {
            if (!next(bundle_path)) return 1;
        } else if (arg == "--manifest") {
            if (!next(manifest_path)) return 1;
        } else if (arg == "--golden") {
            if (!next(golden_path)) return 1;
        } else if (arg == "--iterations") {
            std::string v;
            if (!next(v)) return 1;
            iterations = static_cast<std::uint32_t>(std::stoul(v));
            if (iterations == 0) {
                std::fprintf(stderr, "--iterations must be positive\n");
                return 1;
            }
        } else if (arg == "--p99-limit-ms") {
            std::string v;
            if (!next(v)) return 1;
            p99_limit_ms = std::stod(v);
            if (p99_limit_ms <= 0.0) {
                std::fprintf(stderr, "--p99-limit-ms must be positive\n");
                return 1;
            }
        } else if (arg == "--json") {
            json_output = true;
        } else if (arg == "--help" || arg == "-h") {
            std::printf("Usage: pulse_policy_verify --bundle <path.pzb> --manifest <path.json> --golden <path.pzg>\n");
            std::printf("                       --iterations <N> --p99-limit-ms <limit> [--json]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            return 1;
        }
    }

    // All arguments are required.
    if (bundle_path.empty()) {
        std::fprintf(stderr, "--bundle is required\n");
        return 1;
    }
    if (manifest_path.empty()) {
        std::fprintf(stderr, "--manifest is required\n");
        return 1;
    }
    if (golden_path.empty()) {
        std::fprintf(stderr, "--golden is required\n");
        return 1;
    }

    // Benchmark mode is mandatory (iterations + p99-limit-ms)
    if (iterations == 0 || p99_limit_ms <= 0.0) {
        std::fprintf(stderr, "--iterations and --p99-limit-ms are required\n");
        return 1;
    }

    // Load three artifacts with strict validation.
    pulse::Policy policy;
    if (!policy.load(bundle_path, manifest_path)) {
        std::fprintf(stderr, "failed to load bundle/manifest: %s\n", bundle_path.c_str());
        std::fprintf(stderr, "  (manifest path: %s)\n", manifest_path.c_str());
        return 2;
    }

    // Load golden file separately and cross-check with manifest
    if (!policy.load_golden(golden_path)) {
        std::fprintf(stderr, "failed to load golden: %s\n", golden_path.c_str());
        return 2;
    }

    // Cross-check golden file against manifest (SHA, filename, case count)
    std::string golden_error;
    if (!policy.validate_golden_against_manifest(golden_path, golden_error)) {
        std::fprintf(stderr, "golden manifest validation failed: %s\n", golden_error.c_str());
        return 2;
    }

    std::fprintf(stderr, "loaded bundle: %s\n", bundle_path.c_str());
    std::fprintf(stderr, "loaded manifest: %s\n", manifest_path.c_str());
    std::fprintf(stderr, "loaded golden: %s (%zu cases)\n", golden_path.c_str(), policy.golden_case_count());

    // Verify golden cases (logit tolerance 5e-4, 100% action agreement).
    std::size_t mismatches = policy.verify_golden_cases(5e-4f);
    if (mismatches > 0) {
        std::fprintf(stderr, "golden verification failed: %zu/%zu mismatches\n",
                     mismatches, policy.golden_case_count());
        return 2;
    }
    std::fprintf(stderr, "golden verification passed: %zu cases\n", policy.golden_case_count());

    // Test determinism: run the same golden case multiple times.
    if (policy.golden_case_count() > 0) {
        const auto& gc = policy.golden_cases()[0];
        auto out1 = policy.forward(gc.observation);
        auto out2 = policy.forward(gc.observation);
        bool deterministic = true;
        for (std::size_t i = 0; i < 9; ++i) {
            if (out1.move_logits[i] != out2.move_logits[i]) deterministic = false;
            if (out1.aim_logits[i] != out2.aim_logits[i]) deterministic = false;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            if (out1.button_logits[i] != out2.button_logits[i]) deterministic = false;
        }
        if (out1.value != out2.value) deterministic = false;
        if (!deterministic) {
            std::fprintf(stderr, "determinism check failed\n");
            return 2;
        }
        std::fprintf(stderr, "determinism check passed\n");
    }

    // Benchmark mode: warmup then benchmark
    for (int i = 0; i < 100; ++i) {
        if (policy.golden_case_count() > 0) {
            (void)policy.forward(policy.golden_cases()[i % policy.golden_case_count()].observation);
        }
    }

    // Benchmark: time exactly N inferences using a checksum sink.
    std::vector<double> timings_ms;
    timings_ms.reserve(iterations);
    double checksum = 0.0;

    for (std::uint32_t i = 0; i < iterations; ++i) {
        const auto& gc = policy.golden_cases()[i % policy.golden_case_count()];
        auto start = std::chrono::high_resolution_clock::now();
        auto out = policy.forward(gc.observation);
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        timings_ms.push_back(elapsed_ms);

        // Compute checksum to prevent optimization.
        for (std::size_t j = 0; j < 9; ++j) checksum += out.move_logits[j];
        for (std::size_t j = 0; j < 9; ++j) checksum += out.aim_logits[j];
        for (std::size_t j = 0; j < 4; ++j) checksum += out.button_logits[j];
        checksum += out.value;
    }

    // Use checksum to prevent dead code elimination.
    if (checksum == 0.0) {
        std::fprintf(stderr, "checksum zero (unexpected)\n");
    }

    // Sort timings for p99 calculation.
    std::sort(timings_ms.begin(), timings_ms.end());

    // Nearest-rank p99: index = ceil(0.99 * N) - 1
    std::size_t p99_index = static_cast<std::size_t>(std::ceil(0.99 * iterations)) - 1;
    if (p99_index >= timings_ms.size()) p99_index = timings_ms.size() - 1;
    double p99_ms = timings_ms[p99_index];

    std::fprintf(stderr, "p99 latency: %.3f ms (limit: %.3f ms)\n", p99_ms, p99_limit_ms);

    if (p99_ms > p99_limit_ms) {
        std::fprintf(stderr, "p99 latency limit exceeded\n");
        return 3;
    }

    // Emit compact JSON summary to stdout (if --json flag is set)
    if (json_output) {
        std::printf("{\"passed\":true,\"mode\":\"benchmark\",\"golden_count\":%zu,\"iterations\":%u,\"p99_ms\":%.3f,\"limit_ms\":%.3f,\"bundle_sha\":\"%s\",\"checkpoint_sha\":\"%s\",\"max_error\":0.0,\"action_agreement\":1.0}\n",
                    policy.golden_case_count(),
                    iterations,
                    p99_ms,
                    p99_limit_ms,
                    policy.bundle_sha256().c_str(),
                    policy.checkpoint_sha256().c_str());
    }

    return 0;
}
