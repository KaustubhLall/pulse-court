#pragma once

// Dependency-free FP32 policy network for native deployment.
//
// Loads a Pulse Zero policy bundle (``.pzb`` file) and runs greedy
// inference in pure FP32. No PyTorch, no external dependencies -- only the
// C++ standard library and the bundle format defined in
// ``training/export_policy_bundle.py``.
//
// The network topology is locked: 64-float input → four 512-unit ReLU hidden
// layers → move (9), aim (9), button (4) logit heads + scalar value head.
// Parameter count: 833,047.
//
// Three-artifact format (phase 3):
// - ``.pzb`` — versioned FP32 weights/tensor metadata (this class loads this).
// - ``.manifest.json`` — strict full provenance/schema/raw-file hashes.
// - ``.golden.pzg`` — separate strict golden cases (load_golden()).
//
// The loader is strict: it validates magic/version/dims/names/order/shapes/
// count products/total, finite floats, exact EOF (no trailing bytes), and
// SHA-256. It fails closed on any error, leaving the policy unloaded.
//
// Usage:
//   pulse::Policy policy;
//   if (!policy.load("pulse_zero_policy.pzb")) { /* error */ }
//   auto out = policy.forward(observation);
//   // out.move_logits[9], out.aim_logits[9], out.button_logits[4],
//   // out.value (float)

#include "pulse_ai.hpp"
#include "pulse_policy_adapter.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pulse {

// SHA-256 hex helper (defined in pulse_policy.cpp)
std::string sha256_hex(const unsigned char* data, std::size_t len);

// Forward-declared JSON value types for manifest parsing
enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonNode {
    JsonType type = JsonType::Null;
    std::string string_val;
    std::int64_t int_val = 0;
    double float_val = 0.0;
    bool bool_val = false;
    std::vector<JsonNode> array_val;
    std::vector<std::pair<std::string, JsonNode>> object_val;
};

inline constexpr std::uint32_t kPolicyBundleMagic = 0x42505A50;  // "PZPB" LE
inline constexpr std::uint32_t kPolicyBundleVersion = 1;
inline constexpr std::uint32_t kGoldenMagic = 0x46475A50;  // "PZGF" LE
inline constexpr std::uint32_t kGoldenVersion = 1;
inline constexpr std::size_t kPolicyObsSize = 64;
inline constexpr std::size_t kPolicyHiddenWidth = 512;
inline constexpr std::size_t kPolicyMoveCount = 9;
inline constexpr std::size_t kPolicyAimCount = 9;
inline constexpr std::size_t kPolicyButtonCount = 4;
inline constexpr std::size_t kPolicyParamCount = 833047;
inline constexpr std::size_t kPolicyTensorCount = 16;

struct PolicyOutput {
    std::array<float, kPolicyMoveCount> move_logits{};
    std::array<float, kPolicyAimCount> aim_logits{};
    std::array<float, kPolicyButtonCount> button_logits{};
    float value = 0.0f;
};

struct GoldenCase {
    std::uint64_t seed = 0;
    std::uint8_t player_index = 0;
    std::uint8_t learner_seat = 0;
    std::uint8_t char0 = 0;
    std::uint8_t char1 = 0;
    std::int8_t opponent_kind = 0;
    std::uint32_t sim_tick = 0;
    std::array<float, kPolicyObsSize> observation{};
    std::array<std::uint8_t, kPolicyButtonCount> button_mask{};
    std::array<float, kPolicyMoveCount> move_logits{};
    std::array<float, kPolicyAimCount> aim_logits{};
    std::array<float, kPolicyButtonCount> button_logits{};
    float value = 0.0f;
    std::uint8_t expected_move = 0;
    std::uint8_t expected_aim = 0;
    std::uint8_t expected_button = 0;
};

// Immutable provenance snapshot populated during strict manifest load.
// Contains all required SHA-256 hashes and identity fields for challenge evidence.
struct PolicyProvenance {
    std::string bundle_sha256;          // SHA-256 of the .pzb bundle file
    std::string manifest_sha256;        // SHA-256 of the manifest.json file content
    std::string checkpoint_sha256;      // Training checkpoint SHA-256
    std::string certification_sha256;  // Certification report SHA-256
    std::string dll_sha256;             // Accepted DLL SHA-256
    std::string config_sha256;          // Config hash
    std::string ruleset_sha256;         // Ruleset fingerprint
    std::string simulator_sha256;       // Simulator fingerprint
    
    // Clear all fields (used on failed/repeated load)
    void clear() noexcept {
        bundle_sha256.clear();
        manifest_sha256.clear();
        checkpoint_sha256.clear();
        certification_sha256.clear();
        dll_sha256.clear();
        config_sha256.clear();
        ruleset_sha256.clear();
        simulator_sha256.clear();
    }
    
    // Check if all required fields are populated (no optional empty fallback).
    [[nodiscard]] bool is_populated() const noexcept {
        return !bundle_sha256.empty() &&
               !manifest_sha256.empty() &&
               !checkpoint_sha256.empty() &&
               !certification_sha256.empty() &&
               !dll_sha256.empty() &&
               !config_sha256.empty() &&
               !ruleset_sha256.empty() &&
               !simulator_sha256.empty();
    }

    // Validate all SHA-256 fields are exactly 64 lowercase hex characters.
    // No field may be empty — config, ruleset, and simulator fingerprints
    // are strictly required, not optional.
    [[nodiscard]] bool is_valid() const noexcept {
        auto is_valid_hex64 = [](const std::string& s) -> bool {
            if (s.size() != 64) return false;
            for (char c : s) {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                    return false;
                }
            }
            return true;
        };

        return is_valid_hex64(bundle_sha256) &&
               is_valid_hex64(manifest_sha256) &&
               is_valid_hex64(checkpoint_sha256) &&
               is_valid_hex64(certification_sha256) &&
               is_valid_hex64(dll_sha256) &&
               is_valid_hex64(config_sha256) &&
               is_valid_hex64(ruleset_sha256) &&
               is_valid_hex64(simulator_sha256);
    }
};

class Policy {
public:
    Policy() = default;

    // Production three-artifact load: bundle (.pzb) + manifest (.json).
    // Validates manifest as typed JSON, cross-checks bundle SHA against manifest,
    // validates all required manifest fields (schema names/versions, bundle/golden
    // filenames and hashes, checkpoint SHA/update/bytes/branch, reward contract
    // identity and zero bonuses, config/reward hashes, accepted DLL SHA, raw
    // certification SHA, ABI=1, ruleset=4, agent contract=2, observation size=64,
    // observation/action schema versions, architecture/tensor fingerprint,
    // parameter count=833047, golden count>=256, endian/version, simulator/
    // ruleset fingerprint, and valid 64-lowercase-hex strings). Rejects missing/
    // wrong-type/wrong/duplicate required fields and incompatible compiled constants.
    // Returns false on any error; policy left unloaded.
    [[nodiscard]] bool load(const std::string& bundle_path,
                           const std::string& manifest_path) noexcept;

    // Legacy one-argument preview-only loader (for local testing only).
    // Does NOT validate manifest or production gates. Use the two-argument
    // load() for production deployment.
    [[nodiscard]] bool load_preview(const std::string& bundle_path) noexcept;

    // Load from an in-memory buffer (for testing). Same validation as load_preview().
    // Validates SHA-256, magic, version, dims, tensor names/order/shapes/
    // counts, finite floats, and exact EOF (no trailing bytes before hash).
    [[nodiscard]] bool load_from_bytes(const std::vector<unsigned char>& data) noexcept;

    // Load golden cases from a separate .pzg file. Returns false on any error.
    // Validates SHA-256, magic, version, dims, finite floats, action ranges,
    // and exact EOF.
    [[nodiscard]] bool load_golden(const std::string& path) noexcept;

    // Compute SHA-256 of the golden file (entire file, including embedded suffix).
    [[nodiscard]] std::string compute_golden_sha256(const std::string& path) const noexcept;

    // Validate the loaded golden file against the manifest (SHA256, filename, case count).
    // Must be called after both load() and load_golden() succeed.
    [[nodiscard]] bool validate_golden_against_manifest(const std::string& golden_path,
                                                        std::string& error_msg) noexcept;

    // Load golden cases from an in-memory buffer. Same validation.
    [[nodiscard]] bool load_golden_from_bytes(const std::vector<unsigned char>& data) noexcept;

    // Bounded JSON parser for manifest validation. No external dependency.
    // Parses a strict subset of JSON: objects with string keys, string/int/
    // bool/null values, and nested objects. Rejects duplicate keys, unknown
    // types, and malformed UTF-8. Returns false on any error.
    [[nodiscard]] bool parse_json(const std::string& json_str,
                                 std::string& error_msg) noexcept;

    // Validate manifest fields against compiled constants and bundle SHA.
    // Returns false on any mismatch; error_msg describes the failure.
    [[nodiscard]] bool validate_manifest(const std::string& bundle_sha,
                                        std::string& error_msg) noexcept;

    // Populate provenance snapshot from validated manifest JSON.
    // Returns false on any error; error_msg describes the failure.
    [[nodiscard]] bool populate_provenance(const std::string& manifest_json,
                                          const std::string& bundle_sha,
                                          std::string& error_msg) noexcept;

    // Check if a string is a valid 64-character lowercase hex string.
    [[nodiscard]] static bool is_valid_hex64(const std::string& s) noexcept;

    // Internal JSON parser helpers (for testing only - these expose
    // the JSON parser implementation for unit testing).
    [[nodiscard]] bool parse_json_internal(const std::string& json_str,
                                         std::string& error_msg) noexcept;
    [[nodiscard]] bool validate_manifest_internal(const std::string& bundle_sha,
                                                std::string& error_msg) noexcept;

    // Run a single forward pass. Requires is_loaded(). Returns PolicyOutput
    // with move/aim/button logits and a tanh-squashed value in [-1, 1].
    [[nodiscard]] PolicyOutput forward(const std::array<float, kPolicyObsSize>& obs) const noexcept;

    // Convenience: state → observation → forward → greedy factors.
    // Uses encode_observation and make_action_mask from pulse_ai.
    [[nodiscard]] std::array<std::int8_t, 3>
    greedy_action(const GameState& state, std::size_t player_index) const noexcept;

    // Convenience: state → observation → forward → greedy factors → FrameInput.
    [[nodiscard]] FrameInput
    greedy_frame_input(const GameState& state, std::size_t player_index) const noexcept;

    [[nodiscard]] bool is_loaded() const noexcept { return loaded_; }
    [[nodiscard]] std::uint32_t version() const noexcept { return version_; }
    [[nodiscard]] std::size_t param_count() const noexcept { return param_count_; }
    [[nodiscard]] std::size_t golden_case_count() const noexcept { return golden_cases_.size(); }
    [[nodiscard]] const std::vector<GoldenCase>& golden_cases() const noexcept { return golden_cases_; }
    [[nodiscard]] const std::string& checkpoint_sha256() const noexcept { return checkpoint_sha256_; }
    [[nodiscard]] const std::string& dll_sha256() const noexcept { return dll_sha256_; }
    [[nodiscard]] const std::string& bundle_sha256() const noexcept { return bundle_sha256_; }
    
    // Get the immutable provenance snapshot (populated during strict manifest load)
    [[nodiscard]] const PolicyProvenance& provenance() const noexcept { return provenance_; }

    // Verify all golden cases: run forward on each observation and check the
    // greedy factors match the expected (move, aim, button). Also checks
    // that the stored logits/value match the forward pass within tolerance.
    // Returns the number of mismatches (0 = all pass).
    [[nodiscard]] std::size_t verify_golden_cases(float logit_tolerance = 5e-4f) const noexcept;

private:
    bool loaded_ = false;
    std::uint32_t version_ = 0;
    std::size_t param_count_ = 0;

    // Weights stored as flat arrays matching the PyTorch layout.
    // trunk: 4 Linear layers (weight + bias each).
    struct LinearLayer {
        std::vector<float> weight;  // [out, in]
        std::vector<float> bias;    // [out]
        std::size_t in_features = 0;
        std::size_t out_features = 0;
    };
    std::array<LinearLayer, 4> trunk_;
    LinearLayer move_head_;
    LinearLayer aim_head_;
    LinearLayer button_head_;
    LinearLayer value_head_;

    std::vector<GoldenCase> golden_cases_;
    std::string checkpoint_sha256_;
    std::string dll_sha256_;
    std::string bundle_sha256_;
    
    // Immutable provenance snapshot (populated during strict manifest load)
    PolicyProvenance provenance_;
    
    // Parsed manifest JSON (for validate_manifest to use).
    std::unique_ptr<JsonNode> parsed_manifest_;

    // Internal helpers.
    static void linear(const float* input, const float* weight,
                       const float* bias, float* output,
                       std::size_t in_features, std::size_t out_features) noexcept;
    static void relu(float* x, std::size_t n) noexcept;

private:
    // Parse a tensor from the bundle data at a given offset. Advances offset
    // and ordinal. Returns false on error. Validates the exact canonical name
    // at the given ordinal, shape, elem_count, and finiteness.
    [[nodiscard]] bool parse_tensor(const unsigned char* data, std::size_t size,
                                    std::size_t& offset,
                                    std::size_t& ordinal,
                                    std::vector<float>& out,
                                    std::vector<std::uint32_t>& shape) noexcept;

    // Navigate to a nested key path in parsed JSON (e.g., "bundle.sha256")
    [[nodiscard]] static const JsonNode* navigate_json(const JsonNode& node,
                                                       const std::string& path,
                                                       std::string& error_msg) noexcept;
};

}  // namespace pulse
