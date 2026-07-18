#pragma once

// ===========================================================================
// TEST/DEV ONLY -- NON-CERTIFIED SYNTHETIC BUNDLE GENERATOR.
//
// This file builds in-memory (and on-disk, via pulse_dev_bundle) synthetic
// ".pzpb" policy bundles and ".pzg" golden files for local testing and
// development. The tensors and golden cases it produces are NOT trained
// weights and carry NO provenance/certification -- they must never be used
// for anything resembling production deployment, grading, or evidence of
// model behavior. Nothing produced here can ever satisfy Policy::load()'s
// strict three-artifact manifest validation (checkpoint hashes, golden
// count >= 256, certification SHA, etc).
// ===========================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pulse::devgen {

// ---- low-level byte-buffer writers (little-endian), matching the bundle
// ---- format defined in training/export_policy_bundle.py.
void w_u32(std::vector<unsigned char>& buf, std::uint32_t v);
void w_u16(std::vector<unsigned char>& buf, std::uint16_t v);
void w_u64(std::vector<unsigned char>& buf, std::uint64_t v);
void w_f32(std::vector<unsigned char>& buf, float f);
void w_str(std::vector<unsigned char>& buf, const std::string& s);
void w_tensor(std::vector<unsigned char>& buf, const std::string& name,
              const std::vector<float>& data,
              const std::vector<std::uint32_t>& shape);

// Minimal standalone SHA-256 (raw 32-byte digest). Independent of
// pulse_policy.cpp's internal implementation so this header has no
// dependency on production loader internals.
void sha256(const unsigned char* data, std::size_t len, unsigned char out[32]);

// Selects whether build_synthetic_bundle() emits all-zero weights (the
// original test behavior, where argmax always ties at index 0) or a small
// deterministic non-zero pattern (so different observations can produce
// different actions). Both modes are fully deterministic.
enum class WeightPattern { Zero, Patterned };

// Deterministic tiny non-zero weight value for flat parameter index `index`,
// in roughly [-0.01, 0.01]. Pure function of the index -- no RNG, no state.
float pattern_weight(std::size_t index);

// Build a synthetic ".pzpb" bundle (header + 16 tensors + trailing SHA-256).
// `golden_count` is accepted for call-site compatibility but unused: bundles
// no longer embed golden cases (phase 3 format). `pattern` selects zero vs.
// deterministic-patterned weights.
std::vector<unsigned char> build_synthetic_bundle(
    std::size_t golden_count = 0,
    WeightPattern pattern = WeightPattern::Zero);

// Build a synthetic ".pzg" golden file with `case_count` all-zero
// observations/logits and expected_move/aim/button = 0 (matches an all-zero
// weight bundle's argmax-ties-at-0 behavior).
std::vector<unsigned char> build_synthetic_golden(std::size_t case_count = 4);

// One golden case with caller-supplied content, for pairing with a
// Patterned bundle (whose forward pass is not all-zero).
struct DevGoldenCase {
    std::uint64_t seed = 0;
    std::uint8_t player_index = 0;
    std::uint8_t learner_seat = 0;
    std::uint8_t char0 = 0;
    std::uint8_t char1 = 0;
    std::int8_t opponent_kind = 1;
    std::uint32_t sim_tick = 0;
    std::array<float, 64> observation{};
    std::array<std::uint8_t, 4> button_mask{};
    std::array<float, 9> move_logits{};
    std::array<float, 9> aim_logits{};
    std::array<float, 4> button_logits{};
    float value = 0.0f;
    std::uint8_t expected_move = 0;
    std::uint8_t expected_aim = 0;
    std::uint8_t expected_button = 0;
};

// Build a ".pzg" golden file from explicit case data (used by
// pulse_dev_bundle to pair real forward-pass outputs of a Patterned bundle
// with matching golden entries).
std::vector<unsigned char> build_synthetic_golden_from_cases(
    const std::vector<DevGoldenCase>& cases);

}  // namespace pulse::devgen
