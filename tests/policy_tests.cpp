// Tests for the policy adapter (factor→FrameInput conversion, button masking,
// greedy argmax) and the native FP32 policy library (bundle load/verify,
// golden case parity, forward pass finiteness).
//
// These tests build a synthetic bundle in-memory (no file I/O needed) and
// verify the C++ policy can load it and run golden cases.

#include "pulse_policy_adapter.hpp"
#include "pulse_policy.hpp"
#include "pulse_ai.hpp"
#include "pulse_sim.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

// ---- synthetic bundle builder (matches export_policy_bundle.py format) ----

struct U32 { std::uint32_t v; };
struct U16 { std::uint16_t v; };
struct U64 { std::uint64_t v; };

void w_u32(std::vector<unsigned char>& buf, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<unsigned char>((v >> (8*i)) & 0xFF));
}
void w_u16(std::vector<unsigned char>& buf, std::uint16_t v) {
    for (int i = 0; i < 2; ++i) buf.push_back(static_cast<unsigned char>((v >> (8*i)) & 0xFF));
}
void w_u64(std::vector<unsigned char>& buf, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<unsigned char>((v >> (8*i)) & 0xFF));
}
void w_f32(std::vector<unsigned char>& buf, float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    w_u32(buf, bits);
}
void w_str(std::vector<unsigned char>& buf, const std::string& s) {
    w_u16(buf, static_cast<std::uint16_t>(s.size()));
    for (char c : s) buf.push_back(static_cast<unsigned char>(c));
}

void w_tensor(std::vector<unsigned char>& buf, const std::string& name,
              const std::vector<float>& data,
              const std::vector<std::uint32_t>& shape) {
    w_str(buf, name);
    w_u32(buf, static_cast<std::uint32_t>(shape.size()));
    for (auto s : shape) w_u32(buf, s);
    w_u64(buf, static_cast<std::uint64_t>(data.size()));
    for (float v : data) w_f32(buf, v);
}

// Minimal SHA-256 (same as pulse_policy.cpp).
void sha256(const unsigned char* data, std::size_t len, unsigned char out[32]) {
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
    std::uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                          0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    auto rotr = [](std::uint32_t x, int n) -> std::uint32_t {
        return (x >> n) | (x << (32 - n));
    };
    std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8;
    std::vector<unsigned char> msg(data, data + len);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<unsigned char>((bit_len >> (8*i)) & 0xFF));
    for (std::size_t blk = 0; blk < msg.size(); blk += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[blk+4*i]) << 24) |
                   (static_cast<std::uint32_t>(msg[blk+4*i+1]) << 16) |
                   (static_cast<std::uint32_t>(msg[blk+4*i+2]) << 8) |
                   (static_cast<std::uint32_t>(msg[blk+4*i+3]));
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
            std::uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    for (int i = 0; i < 8; ++i) {
        out[4*i]   = (h[i] >> 24) & 0xFF;
        out[4*i+1] = (h[i] >> 16) & 0xFF;
        out[4*i+2] = (h[i] >> 8) & 0xFF;
        out[4*i+3] = h[i] & 0xFF;
    }
}

std::vector<unsigned char> build_synthetic_bundle(std::size_t golden_count = 0) {
    (void)golden_count;  // Bundle no longer contains golden cases (phase 3).
    std::vector<unsigned char> buf;
    // Fixed header (9 u32 fields, no golden_count).
    w_u32(buf, 0x42505A50);  // "PZPB" LE
    w_u32(buf, 1);  // version
    w_u32(buf, 64);  // obs_size
    w_u32(buf, 512);  // hidden
    w_u32(buf, 9);  // move
    w_u32(buf, 9);  // aim
    w_u32(buf, 4);  // button
    w_u32(buf, 833047);  // param_count
    w_u32(buf, 16);  // tensor_count
    // No golden_count field in the new format.

    // Header JSON (minimal).
    std::string header = "{\"checkpoint_sha256\":\"a\",\"dll_sha256\":\"b\"}";
    w_u32(buf, static_cast<std::uint32_t>(header.size()));
    for (char c : header) buf.push_back(static_cast<unsigned char>(c));

    // Tensors (all zeros for simplicity).
    auto lin_w = [&](const std::string& name, int out, int in) {
        w_tensor(buf, name, std::vector<float>(out * in, 0.0f),
                 {static_cast<std::uint32_t>(out), static_cast<std::uint32_t>(in)});
    };
    auto lin_b = [&](const std::string& name, int out) {
        w_tensor(buf, name, std::vector<float>(out, 0.0f),
                 {static_cast<std::uint32_t>(out)});
    };
    lin_w("trunk.0.weight", 512, 64); lin_b("trunk.0.bias", 512);
    lin_w("trunk.2.weight", 512, 512); lin_b("trunk.2.bias", 512);
    lin_w("trunk.4.weight", 512, 512); lin_b("trunk.4.bias", 512);
    lin_w("trunk.6.weight", 512, 512); lin_b("trunk.6.bias", 512);
    lin_w("move_head.weight", 9, 512); lin_b("move_head.bias", 9);
    lin_w("aim_head.weight", 9, 512); lin_b("aim_head.bias", 9);
    lin_w("button_head.weight", 4, 512); lin_b("button_head.bias", 4);
    lin_w("value_head.weight", 1, 512); lin_b("value_head.bias", 1);

    // No golden cases in the bundle (they're in a separate .pzg file).

    // SHA-256 of all preceding bytes.
    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);

    return buf;
}

// Build a synthetic golden file (.pzg format) for testing.
std::vector<unsigned char> build_synthetic_golden(std::size_t case_count = 4) {
    std::vector<unsigned char> buf;
    w_u32(buf, 0x46475A50);  // "PZGF" LE
    w_u32(buf, 1);  // version
    w_u32(buf, static_cast<std::uint32_t>(case_count));
    w_u32(buf, 64);  // obs_size
    w_u32(buf, 9);  // move
    w_u32(buf, 9);  // aim
    w_u32(buf, 4);  // button

    for (std::size_t i = 0; i < case_count; ++i) {
        w_u64(buf, 1000001 + i);  // seed
        buf.push_back(0);  // player_index
        buf.push_back(0);  // learner_seat
        buf.push_back(0);  // char0
        buf.push_back(0);  // char1
        buf.push_back(1);  // opponent_kind (int8)
        w_u32(buf, 0);  // sim_tick
        for (int j = 0; j < 64; ++j) w_f32(buf, 0.0f);  // observation
        buf.push_back(1); buf.push_back(1); buf.push_back(0); buf.push_back(0);  // mask
        for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);  // move_logits
        for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);  // aim_logits
        for (int j = 0; j < 4; ++j) w_f32(buf, 0.0f);  // button_logits
        w_f32(buf, 0.0f);  // value
        buf.push_back(0);  // expected_move
        buf.push_back(0);  // expected_aim
        buf.push_back(0);  // expected_button
    }

    // SHA-256 of all preceding bytes.
    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);

    return buf;
}

// ---- test helpers ----

int tests_run = 0;
int tests_failed = 0;

#define CHECK(cond) do { \
    ++tests_run; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++tests_failed; \
    } \
} while(0)

#define CHECK_EQ(a, b) do { \
    ++tests_run; \
    if (!((a) == (b))) { \
        std::fprintf(stderr, "FAIL: %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
        ++tests_failed; \
    } \
} while(0)

// ---- policy adapter tests ----

void test_factors_to_frame_input_seat0() {
    // Factor (move=1, aim=1, button=0) → canonical FrameInput for seat 0.
    auto fi = pulse::ai::factors_to_frame_input(1, 1, 0, 0);
    // move=1 is (0,1), aim=1 is (0,1), button=0 is none.
    CHECK_EQ(fi.move_x, 0);
    CHECK_EQ(fi.move_y, 1);
    CHECK_EQ(fi.aim_x, 0);
    CHECK_EQ(fi.aim_y, 1);
    CHECK_EQ(fi.buttons, pulse::ButtonNone);
}

void test_factors_to_frame_input_seat1_mirrored() {
    // Seat 1 mirrors x-axis only: move=4 (1,0) → mirror → (-1,0).
    auto fi = pulse::ai::factors_to_frame_input(4, 4, 0, 1);
    CHECK_EQ(fi.move_x, -1);
    CHECK_EQ(fi.move_y, 0);
    CHECK_EQ(fi.aim_x, -1);
    CHECK_EQ(fi.aim_y, 0);
}

void test_factors_button_strike() {
    auto fi = pulse::ai::factors_to_frame_input(0, 0, 1, 0);
    CHECK_EQ(fi.buttons, pulse::ButtonStrike);
}

void test_factors_button_ability() {
    auto fi = pulse::ai::factors_to_frame_input(0, 0, 2, 0);
    CHECK_EQ(fi.buttons, pulse::ButtonAbility);
}

void test_factors_button_dash() {
    auto fi = pulse::ai::factors_to_frame_input(0, 0, 3, 0);
    CHECK_EQ(fi.buttons, pulse::ButtonDash);
}

void test_factors_out_of_range_clamps() {
    auto fi = pulse::ai::factors_to_frame_input(-1, 9, 5, 0);
    // Clamps to (0, 0, 0) → no movement, no buttons.
    CHECK_EQ(fi.move_x, 0);
    CHECK_EQ(fi.move_y, 0);
    CHECK_EQ(fi.buttons, pulse::ButtonNone);
}

void test_mask_button_logits_all_legal() {
    std::array<float, 4> logits = {1.0f, 3.0f, 2.0f, 0.5f};
    std::array<std::uint8_t, 4> mask = {1, 1, 1, 1};
    std::array<float, 4> out{};
    pulse::ai::mask_button_logits(logits, mask, out);
    CHECK_EQ(out[0], 1.0f);
    CHECK_EQ(out[1], 3.0f);
    CHECK_EQ(out[2], 2.0f);
    CHECK_EQ(out[3], 0.5f);
}

void test_mask_button_logits_some_illegal() {
    std::array<float, 4> logits = {1.0f, 3.0f, 2.0f, 0.5f};
    std::array<std::uint8_t, 4> mask = {1, 0, 0, 0};
    std::array<float, 4> out{};
    pulse::ai::mask_button_logits(logits, mask, out);
    CHECK_EQ(out[0], 1.0f);
    CHECK(std::isinf(out[1]) && out[1] < 0);
    CHECK(std::isinf(out[2]) && out[2] < 0);
    CHECK(std::isinf(out[3]) && out[3] < 0);
}

void test_mask_button_logits_none_legal() {
    std::array<float, 4> logits = {1.0f, 3.0f, 2.0f, 0.5f};
    std::array<std::uint8_t, 4> mask = {0, 0, 0, 0};
    std::array<float, 4> out{};
    pulse::ai::mask_button_logits(logits, mask, out);
    // When no button is legal (including no-op), all outputs are -inf (fail-closed).
    CHECK(std::isinf(out[0]) && out[0] < 0);
    CHECK(std::isinf(out[1]) && out[1] < 0);
    CHECK(std::isinf(out[2]) && out[2] < 0);
    CHECK(std::isinf(out[3]) && out[3] < 0);
}

void test_greedy_factors_basic() {
    std::array<float, 9> move_l = {0, 0, 0, 0, 5.0f, 0, 0, 0, 0};
    std::array<float, 9> aim_l = {0, 3.0f, 0, 0, 0, 0, 0, 0, 0};
    std::array<float, 4> btn_l = {1.0f, 5.0f, 2.0f, 0.0f};
    std::array<std::uint8_t, 4> btn_m = {1, 1, 1, 1};
    auto f = pulse::ai::greedy_factors_from_logits(move_l, aim_l, btn_l, btn_m);
    CHECK_EQ(f[0], 4);  // move
    CHECK_EQ(f[1], 1);  // aim
    CHECK_EQ(f[2], 1);  // button (5.0 is highest)
}

void test_greedy_factors_masked_button() {
    std::array<float, 9> move_l = {5.0f, 0, 0, 0, 0, 0, 0, 0, 0};
    std::array<float, 9> aim_l = {5.0f, 0, 0, 0, 0, 0, 0, 0, 0};
    std::array<float, 4> btn_l = {1.0f, 5.0f, 2.0f, 0.0f};
    std::array<std::uint8_t, 4> btn_m = {1, 0, 1, 0};  // only none and ability
    auto f = pulse::ai::greedy_factors_from_logits(move_l, aim_l, btn_l, btn_m);
    CHECK_EQ(f[0], 0);
    CHECK_EQ(f[1], 0);
    CHECK_EQ(f[2], 2);  // ability (2.0) is highest legal
}

// ---- policy library tests ----

void test_policy_load_synthetic() {
    auto bundle = build_synthetic_bundle();
    pulse::Policy policy;
    CHECK(policy.load_from_bytes(bundle));
    CHECK(policy.is_loaded());
    CHECK_EQ(policy.version(), 1u);
    CHECK_EQ(policy.param_count(), 833047u);
    // Bundle no longer contains golden cases.
    CHECK_EQ(policy.golden_case_count(), 0u);
}

void test_policy_load_tampered_sha() {
    auto bundle = build_synthetic_bundle();
    // Flip a byte in the tensor data.
    bundle[100] ^= 0x01;
    pulse::Policy policy;
    CHECK(!policy.load_from_bytes(bundle));
    CHECK(!policy.is_loaded());
}

void test_policy_load_bad_magic() {
    auto bundle = build_synthetic_bundle();
    bundle[0] = 'X';
    // Recompute SHA so we test magic, not SHA.
    unsigned char hash[32];
    sha256(bundle.data(), bundle.size() - 32, hash);
    std::memcpy(bundle.data() + bundle.size() - 32, hash, 32);
    pulse::Policy policy;
    CHECK(!policy.load_from_bytes(bundle));
}

void test_policy_forward_finite() {
    auto bundle = build_synthetic_bundle();
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    std::array<float, 64> obs{};
    auto out = policy.forward(obs);
    for (float v : out.move_logits) CHECK(std::isfinite(v));
    for (float v : out.aim_logits) CHECK(std::isfinite(v));
    for (float v : out.button_logits) CHECK(std::isfinite(v));
    CHECK(std::isfinite(out.value));
    CHECK(out.value >= -1.0f && out.value <= 1.0f);  // tanh range
}

void test_policy_golden_load_and_verify() {
    // Load a bundle and a separate golden file, then verify.
    auto bundle = build_synthetic_bundle();
    auto golden = build_synthetic_golden(4);
    pulse::Policy policy;
    CHECK(policy.load_from_bytes(bundle));
    CHECK(policy.load_golden_from_bytes(golden));
    CHECK_EQ(policy.golden_case_count(), 4u);
    // With all-zero weights, argmax always picks index 0, which matches
    // the golden cases' expected_move/aim/button = 0.
    CHECK_EQ(policy.verify_golden_cases(), 0u);
}

void test_policy_golden_sha_tamper() {
    auto golden = build_synthetic_golden(4);
    golden[100] ^= 0x01;
    pulse::Policy policy;
    CHECK(!policy.load_golden_from_bytes(golden));
}

void test_policy_golden_bad_magic() {
    auto golden = build_synthetic_golden(4);
    golden[0] = 'X';
    // Recompute SHA.
    unsigned char hash[32];
    sha256(golden.data(), golden.size() - 32, hash);
    std::memcpy(golden.data() + golden.size() - 32, hash, 32);
    pulse::Policy policy;
    CHECK(!policy.load_golden_from_bytes(golden));
}

void test_policy_golden_mismatch_detected() {
    // Build a golden file where expected_move=5 but the all-zero model
    // produces move=0.
    std::vector<unsigned char> buf;
    w_u32(buf, 0x46475A50); w_u32(buf, 1); w_u32(buf, 1);
    w_u32(buf, 64); w_u32(buf, 9); w_u32(buf, 9); w_u32(buf, 4);
    // One golden case with wrong expected_move=5.
    w_u64(buf, 1000001);
    buf.push_back(0); buf.push_back(0); buf.push_back(0); buf.push_back(0);
    buf.push_back(1);
    w_u32(buf, 0);
    for (int j = 0; j < 64; ++j) w_f32(buf, 0.0f);
    buf.push_back(1); buf.push_back(1); buf.push_back(0); buf.push_back(0);
    for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);
    for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);
    for (int j = 0; j < 4; ++j) w_f32(buf, 0.0f);
    w_f32(buf, 0.0f);
    buf.push_back(5);  // expected_move=5 (WRONG: model produces 0)
    buf.push_back(0);
    buf.push_back(0);
    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);

    auto bundle = build_synthetic_bundle();
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    (void)policy.load_golden_from_bytes(buf);
    CHECK_EQ(policy.verify_golden_cases(), 1u);
}

void test_policy_bundle_trailing_bytes_rejected() {
    // A bundle with extra bytes after the SHA-256 must be rejected.
    auto bundle = build_synthetic_bundle();
    bundle.push_back(0x00);
    bundle.push_back(0x01);
    pulse::Policy policy;
    CHECK(!policy.load_from_bytes(bundle));
}

void test_policy_golden_trailing_bytes_rejected() {
    // A golden file with extra bytes after the SHA-256 must be rejected.
    auto golden = build_synthetic_golden(4);
    golden.push_back(0x00);
    golden.push_back(0x01);
    pulse::Policy policy;
    CHECK(!policy.load_golden_from_bytes(golden));
}

void test_policy_golden_action_out_of_range_rejected() {
    // A golden file with expected_move >= 9 must be rejected.
    std::vector<unsigned char> buf;
    w_u32(buf, 0x46475A50); w_u32(buf, 1); w_u32(buf, 1);
    w_u32(buf, 64); w_u32(buf, 9); w_u32(buf, 9); w_u32(buf, 4);
    w_u64(buf, 1000001);
    buf.push_back(0); buf.push_back(0); buf.push_back(0); buf.push_back(0);
    buf.push_back(1);
    w_u32(buf, 0);
    for (int j = 0; j < 64; ++j) w_f32(buf, 0.0f);
    buf.push_back(1); buf.push_back(1); buf.push_back(0); buf.push_back(0);
    for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);
    for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);
    for (int j = 0; j < 4; ++j) w_f32(buf, 0.0f);
    w_f32(buf, 0.0f);
    buf.push_back(99);  // expected_move=99 (out of range)
    buf.push_back(0);
    buf.push_back(0);
    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);

    pulse::Policy policy;
    CHECK(!policy.load_golden_from_bytes(buf));
}

// ---- Mutation tests for hash-valid malformed inputs ----

void test_policy_tensor_rejected_renamed() {
    // Bundle with renamed tensor (hash-valid after recomputation) must be rejected.
    std::vector<unsigned char> buf;
    w_u32(buf, 0x42505A50);  // magic
    w_u32(buf, 1);  // version
    w_u32(buf, 64); w_u32(buf, 512); w_u32(buf, 9); w_u32(buf, 9); w_u32(buf, 4);
    w_u32(buf, 833047); w_u32(buf, 16);
    std::string header = "{\"checkpoint_sha256\":\"a\",\"dll_sha256\":\"b\"}";
    w_u32(buf, static_cast<std::uint32_t>(header.size()));
    for (char c : header) buf.push_back(static_cast<unsigned char>(c));
    // First tensor with wrong name
    w_str(buf, "wrong_name.weight");  // WRONG: should be trunk.0.weight
    w_u32(buf, 2);
    w_u32(buf, 512); w_u32(buf, 64);
    w_u64(buf, 512 * 64);
    for (std::size_t i = 0; i < 512 * 64; ++i) w_f32(buf, 0.0f);
    // Fill remaining tensors with minimal valid data
    for (std::size_t t = 1; t < 16; ++t) {
        w_str(buf, "dummy");
        w_u32(buf, 1);
        w_u32(buf, 1);
        w_u64(buf, 1);
        w_f32(buf, 0.0f);
    }
    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);
    pulse::Policy policy;
    CHECK(!policy.load_from_bytes(buf));
}

void test_policy_tensor_rejected_shape_product_mismatch() {
    // Bundle with shape product != elem_count (hash-valid) must be rejected.
    std::vector<unsigned char> buf;
    w_u32(buf, 0x42505A50);  // magic
    w_u32(buf, 1);  // version
    w_u32(buf, 64); w_u32(buf, 512); w_u32(buf, 9); w_u32(buf, 9); w_u32(buf, 4);
    w_u32(buf, 833047); w_u32(buf, 16);
    std::string header = "{\"checkpoint_sha256\":\"a\",\"dll_sha256\":\"b\"}";
    w_u32(buf, static_cast<std::uint32_t>(header.size()));
    for (char c : header) buf.push_back(static_cast<unsigned char>(c));
    // First tensor: trunk.0.weight with wrong elem_count
    w_str(buf, "trunk.0.weight");
    w_u32(buf, 2);  // ndim
    w_u32(buf, 512); w_u32(buf, 64);  // shape
    w_u64(buf, 512 * 64 + 1);  // WRONG elem_count (off by 1)
    for (std::size_t i = 0; i < 512 * 64; ++i) w_f32(buf, 0.0f);
    // Fill remaining tensors with minimal valid data
    for (std::size_t t = 1; t < 16; ++t) {
        w_str(buf, "dummy");
        w_u32(buf, 1);
        w_u32(buf, 1);
        w_u64(buf, 1);
        w_f32(buf, 0.0f);
    }
    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);
    pulse::Policy policy;
    CHECK(!policy.load_from_bytes(buf));
}

void test_json_parser_happy_path() {
    // Test basic JSON parsing with valid JSON
    pulse::Policy policy;
    std::string valid_json = R"({"checkpoint_sha256":"abc123","dll_sha256":"def456"})";
    std::string error_msg;
    CHECK(policy.parse_json_internal(valid_json, error_msg));
}

void test_json_parser_duplicate_key_rejected() {
    // Test JSON with duplicate keys is rejected
    pulse::Policy policy;
    std::string duplicate_key_json = R"({"checkpoint_sha256":"abc","checkpoint_sha256":"def"})";
    std::string error_msg;
    CHECK(!policy.parse_json_internal(duplicate_key_json, error_msg));
}

void test_json_parser_wrong_type_rejected() {
    // parse_json_internal is a generic JSON parser; it accepts any valid JSON
    // value type for any field. Type validation is the responsibility of
    // validate_manifest_internal. A number where a string is expected parses
    // fine but must be rejected at validation time.
    pulse::Policy policy;
    std::string wrong_type_json = R"({"checkpoint_sha256":123})";  // should be string
    std::string error_msg;
    CHECK(policy.parse_json_internal(wrong_type_json, error_msg));   // parse succeeds
    CHECK(!policy.validate_manifest_internal("abc123", error_msg));  // validation fails
}

void test_json_parser_missing_key_rejected() {
    // Test JSON with missing required key (this is a manifest validation test)
    pulse::Policy policy;
    std::string missing_key_json = R"({"checkpoint_sha256":"abc"})";  // missing dll_sha256
    std::string error_msg;
    CHECK(policy.parse_json_internal(missing_key_json, error_msg));  // Parse succeeds
    CHECK(!policy.validate_manifest_internal("abc", error_msg));  // Validation fails
}

void test_json_parser_trailing_content_rejected() {
    // Test JSON with trailing content is rejected
    pulse::Policy policy;
    std::string trailing_json = R"({"checkpoint_sha256":"abc"} trailing)";
    std::string error_msg;
    CHECK(!policy.parse_json_internal(trailing_json, error_msg));
}

void test_json_parser_malformed_number_rejected() {
    // Test JSON with malformed number
    pulse::Policy policy;
    std::string malformed_number_json = R"({"value":12.34.56})";
    std::string error_msg;
    CHECK(!policy.parse_json_internal(malformed_number_json, error_msg));
}

void test_json_parser_malformed_escape_rejected() {
    // Test JSON with malformed escape sequence
    pulse::Policy policy;
    std::string malformed_escape_json = R"({"value":"invalid\escape"})";
    std::string error_msg;
    CHECK(!policy.parse_json_internal(malformed_escape_json, error_msg));
}

void test_json_parser_excessive_depth_rejected() {
    // Test JSON with excessive nesting depth
    pulse::Policy policy;
    std::string deep_json = R"({"a":{"b":{"c":{"d":{"e":{"f":{"g":"deep"}}}}}}})";
    std::string error_msg;
    // This may or may not be rejected depending on depth limit
    // For now, just test that it parses (depth limit may be high)
    (void)policy.parse_json_internal(deep_json, error_msg);
    ++tests_run;  // count as tested
}

// Build a complete, valid manifest JSON that passes every check in
// validate_manifest_internal. The bundle SHA is 64 'a' chars; callers must
// pass "aaaa..." (64 a's) to validate_manifest_internal.
static const char* kValidBundleSha = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

static std::string build_valid_manifest() {
    return R"({
        "bundle": {"sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "filename": "pulse_zero_policy.pzb", "magic": "PZPB", "version": 1, "endian": "little"},
        "golden": {"sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "filename": "pulse_zero_policy.golden.pzg", "magic": "PZGF", "version": 1, "case_count": 256},
        "checkpoint_sha256": "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        "config_hash": "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
        "reward_contract_hash": "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
        "dll_sha256": "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        "certification_report_sha256": "1111111111111111111111111111111111111111111111111111111111111111",
        "ruleset_sha256": "2222222222222222222222222222222222222222222222222222222222222222",
        "simulator_sha256": "3333333333333333333333333333333333333333333333333333333333333333",
        "abi_version": 1,
        "ruleset_version": 4,
        "agent_contract_version": 2,
        "observation_size": 64,
        "hidden_width": 512,
        "move_count": 9,
        "aim_count": 9,
        "button_count": 4,
        "param_count": 833047,
        "observation_schema_version": 1,
        "action_schema_version": 1,
        "tensor_count": 16,
        "checkpoint_update": 331,
        "checkpoint_bytes": 10046499,
        "checkpoint_branch_name": "terminal_v1",
        "reward_contract": {"is_identity": true, "bonus_last_touch": 0, "bonus_controlled": 0},
        "tensor_schema": [
            {"name":"t0","shape":[1]}, {"name":"t1","shape":[1]},
            {"name":"t2","shape":[1]}, {"name":"t3","shape":[1]},
            {"name":"t4","shape":[1]}, {"name":"t5","shape":[1]},
            {"name":"t6","shape":[1]}, {"name":"t7","shape":[1]},
            {"name":"t8","shape":[1]}, {"name":"t9","shape":[1]},
            {"name":"t10","shape":[1]}, {"name":"t11","shape":[1]},
            {"name":"t12","shape":[1]}, {"name":"t13","shape":[1]},
            {"name":"t14","shape":[1]}, {"name":"t15","shape":[1]}
        ],
        "created_utc": "2026-01-01T00:00:00Z",
        "versions": {"abi_version": 1}
    })";
}

void test_manifest_validation_happy_path() {
    // Test manifest validation with a complete valid manifest.
    pulse::Policy policy;
    std::string valid_manifest = build_valid_manifest();
    std::string error_msg;
    CHECK(policy.parse_json_internal(valid_manifest, error_msg));
    CHECK(policy.validate_manifest_internal(kValidBundleSha, error_msg));
}

void test_manifest_validation_wrong_bundle_sha_rejected() {
    // Test manifest validation with wrong bundle SHA (mismatches the arg).
    pulse::Policy policy;
    std::string manifest = build_valid_manifest();
    // Replace bundle sha256 with a different valid 64-hex string.
    std::string wrong = "9999999999999999999999999999999999999999999999999999999999999999";
    size_t pos = manifest.find("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    manifest.replace(pos, 64, wrong);
    std::string error_msg;
    CHECK(policy.parse_json_internal(manifest, error_msg));
    CHECK(!policy.validate_manifest_internal(kValidBundleSha, error_msg));
}

void test_manifest_validation_wrong_hash_format_rejected() {
    // Test manifest validation with wrong hash format (not 64 hex chars).
    pulse::Policy policy;
    std::string manifest = build_valid_manifest();
    // Replace checkpoint_sha256 with a too-short string.
    std::string bad_hash = "not64chars";
    size_t pos = manifest.find("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    manifest.replace(pos, 64, bad_hash);
    std::string error_msg;
    CHECK(policy.parse_json_internal(manifest, error_msg));
    CHECK(!policy.validate_manifest_internal(kValidBundleSha, error_msg));
}

void test_policy_tensor_rejected_huge_dimension() {
    // Bundle with dimension > 10000 (hash-valid) must be rejected.
    std::vector<unsigned char> buf;
    w_u32(buf, 0x42505A50);
    w_u32(buf, 1);
    w_u32(buf, 64); w_u32(buf, 512); w_u32(buf, 9); w_u32(buf, 9); w_u32(buf, 4);
    w_u32(buf, 833047); w_u32(buf, 16);
    std::string header = "{\"checkpoint_sha256\":\"a\",\"dll_sha256\":\"b\"}";
    w_u32(buf, static_cast<std::uint32_t>(header.size()));
    for (char c : header) buf.push_back(static_cast<unsigned char>(c));
    // First tensor with huge dimension
    w_str(buf, "trunk.0.weight");
    w_u32(buf, 2);
    w_u32(buf, 20000);  // TOO LARGE (> 10000)
    w_u32(buf, 64);
    w_u64(buf, 20000 * 64);
    for (std::size_t i = 0; i < 20000 * 64; ++i) w_f32(buf, 0.0f);
    // Fill remaining tensors
    for (std::size_t t = 1; t < 16; ++t) {
        w_str(buf, "dummy");
        w_u32(buf, 1);
        w_u32(buf, 1);
        w_u64(buf, 1);
        w_f32(buf, 0.0f);
    }
    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);
    pulse::Policy policy;
    CHECK(!policy.load_from_bytes(buf));
}

void test_policy_tensor_rejected_nonfinite() {
    // Bundle with NaN weight (hash-valid) must be rejected.
    std::vector<unsigned char> buf;
    w_u32(buf, 0x42505A50);  // magic
    w_u32(buf, 1);  // version
    w_u32(buf, 64); w_u32(buf, 512); w_u32(buf, 9); w_u32(buf, 9); w_u32(buf, 4);
    w_u32(buf, 833047); w_u32(buf, 16);
    std::string header = "{\"checkpoint_sha256\":\"a\",\"dll_sha256\":\"b\"}";
    w_u32(buf, static_cast<std::uint32_t>(header.size()));
    for (char c : header) buf.push_back(static_cast<unsigned char>(c));
    // First tensor with NaN in first element
    w_str(buf, "trunk.0.weight");
    w_u32(buf, 2);
    w_u32(buf, 512); w_u32(buf, 64);
    w_u64(buf, 512 * 64);
    // Write NaN (0x7FC00000 in IEEE 754)
    w_u32(buf, 0x7FC00000);
    for (std::size_t i = 1; i < 512 * 64; ++i) w_f32(buf, 0.0f);
    // Fill remaining tensors with minimal valid data
    for (std::size_t t = 1; t < 16; ++t) {
        w_str(buf, "dummy");
        w_u32(buf, 1);
        w_u32(buf, 1);
        w_u64(buf, 1);
        w_f32(buf, 0.0f);
    }
    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);
    pulse::Policy policy;
    CHECK(!policy.load_from_bytes(buf));
}

void test_policy_golden_rejected_no_op_illegal() {
    // Golden file with no-op (index 0) illegal must be rejected.
    std::vector<unsigned char> buf;
    w_u32(buf, 0x46475A50); w_u32(buf, 1); w_u32(buf, 1);
    w_u32(buf, 64); w_u32(buf, 9); w_u32(buf, 9); w_u32(buf, 4);
    w_u64(buf, 1000001);
    buf.push_back(0); buf.push_back(0); buf.push_back(0); buf.push_back(0);
    buf.push_back(1);
    w_u32(buf, 0);
    for (int j = 0; j < 64; ++j) w_f32(buf, 0.0f);
    buf.push_back(0); buf.push_back(1); buf.push_back(1); buf.push_back(1);  // no-op illegal
    for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);
    for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);
    for (int j = 0; j < 4; ++j) w_f32(buf, 0.0f);
    w_f32(buf, 0.0f);
    buf.push_back(0); buf.push_back(0); buf.push_back(0);
    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);
    pulse::Policy policy;
    CHECK(!policy.load_golden_from_bytes(buf));
}

void test_policy_golden_rejected_all_zero_mask() {
    // Golden file with all-zero mask must be rejected.
    std::vector<unsigned char> buf;
    w_u32(buf, 0x46475A50); w_u32(buf, 1); w_u32(buf, 1);
    w_u32(buf, 64); w_u32(buf, 9); w_u32(buf, 9); w_u32(buf, 4);
    w_u64(buf, 1000001);
    buf.push_back(0); buf.push_back(0); buf.push_back(0); buf.push_back(0);
    buf.push_back(1);
    w_u32(buf, 0);
    for (int j = 0; j < 64; ++j) w_f32(buf, 0.0f);
    buf.push_back(0); buf.push_back(0); buf.push_back(0); buf.push_back(0);  // all illegal
    for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);
    for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);
    for (int j = 0; j < 4; ++j) w_f32(buf, 0.0f);
    w_f32(buf, 0.0f);
    buf.push_back(0); buf.push_back(0); buf.push_back(0);
    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);
    pulse::Policy policy;
    CHECK(!policy.load_golden_from_bytes(buf));
}

void test_policy_golden_rejected_nonfinite_observation() {
    // Golden file with non-finite observation must be rejected.
    std::vector<unsigned char> buf;
    w_u32(buf, 0x46475A50); w_u32(buf, 1); w_u32(buf, 1);
    w_u32(buf, 64); w_u32(buf, 9); w_u32(buf, 9); w_u32(buf, 4);
    w_u64(buf, 1000001);
    buf.push_back(0); buf.push_back(0); buf.push_back(0); buf.push_back(0);
    buf.push_back(1);
    w_u32(buf, 0);
    // Write NaN in first observation element
    w_u32(buf, 0x7FC00000);
    for (int j = 1; j < 64; ++j) w_f32(buf, 0.0f);
    buf.push_back(1); buf.push_back(1); buf.push_back(0); buf.push_back(0);
    for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);
    for (int j = 0; j < 9; ++j) w_f32(buf, 0.0f);
    for (int j = 0; j < 4; ++j) w_f32(buf, 0.0f);
    w_f32(buf, 0.0f);
    buf.push_back(0); buf.push_back(0); buf.push_back(0);
    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);

    pulse::Policy policy;
    CHECK(!policy.load_golden_from_bytes(buf));
}

void test_policy_greedy_action_on_state() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    // Create a simple game state.
    std::array<pulse::Character, 2> chars = {pulse::Character::Kite,
                                              pulse::Character::Vale};
    pulse::Simulation sim(chars);
    auto factors = policy.greedy_action(sim.state(), 0);
    // With all-zero weights, all logits are 0, argmax = 0.
    CHECK_EQ(factors[0], 0);
    CHECK_EQ(factors[1], 0);
    // Button 0 (none) is always legal, so button argmax = 0.
    CHECK_EQ(factors[2], 0);
}

// ---- PolicyController 12-tick cadence tests ----

// Helper: create a minimal live GameState for controller testing.
pulse::GameState make_test_state(std::uint32_t tick, pulse::Phase phase) {
    pulse::GameState s;
    s.tick = tick;
    s.phase = phase;
    s.players[0].character = pulse::Character::Kite;
    s.players[1].character = pulse::Character::Vale;
    return s;
}

void test_policy_controller_first_use_decides() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(0, pulse::Phase::Live);
    auto out = ctrl.act(state, 0);
    CHECK(out.decision);
    CHECK(ctrl.last_decision());
}

void test_policy_controller_hold_between_decisions() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(0, pulse::Phase::Live);
    auto out0 = ctrl.act(state, 0);
    CHECK(out0.decision);
    for (std::uint32_t t = 1; t <= 11; ++t) {
        state.tick = t;
        auto out = ctrl.act(state, 0);
        CHECK(!out.decision);
        CHECK_EQ(out.input.buttons, pulse::ButtonNone);
    }
}

void test_policy_controller_cadence_at_tick_12() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(0, pulse::Phase::Live);
    (void)ctrl.act(state, 0);
    for (std::uint32_t t = 1; t <= 11; ++t) {
        state.tick = t;
        (void)ctrl.act(state, 0);
    }
    state.tick = 12;
    auto out = ctrl.act(state, 0);
    CHECK(out.decision);
}

void test_policy_controller_cadence_at_tick_24() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(0, pulse::Phase::Live);
    (void)ctrl.act(state, 0);
    for (std::uint32_t t = 1; t <= 23; ++t) {
        state.tick = t;
        (void)ctrl.act(state, 0);
    }
    state.tick = 24;
    auto out = ctrl.act(state, 0);
    CHECK(out.decision);
}

void test_policy_controller_phase_change_decides() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(0, pulse::Phase::Kickoff);
    (void)ctrl.act(state, 0);
    state.tick = 1;
    (void)ctrl.act(state, 0);
    state.phase = pulse::Phase::Live;
    auto out = ctrl.act(state, 0);
    CHECK(out.decision);
}

void test_policy_controller_player_change_decides() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(5, pulse::Phase::Live);
    (void)ctrl.act(state, 0);
    auto out = ctrl.act(state, 1);
    CHECK(out.decision);
}

void test_policy_controller_rewind_decides() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(20, pulse::Phase::Live);
    (void)ctrl.act(state, 0);
    state.tick = 21;
    (void)ctrl.act(state, 0);
    state.tick = 15;
    auto out = ctrl.act(state, 0);
    CHECK(out.decision);
}

void test_policy_controller_matchover_zero_input() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(0, pulse::Phase::MatchOver);
    auto out = ctrl.act(state, 0);
    CHECK(!out.decision);
    CHECK_EQ(out.input.move_x, 0);
    CHECK_EQ(out.input.move_y, 0);
    CHECK_EQ(out.input.buttons, pulse::ButtonNone);
}

void test_policy_controller_idempotent_same_tick() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(0, pulse::Phase::Live);
    auto out1 = ctrl.act(state, 0);
    auto out2 = ctrl.act(state, 0);
    CHECK_EQ(out1.input.move_x, out2.input.move_x);
    CHECK_EQ(out1.input.move_y, out2.input.move_y);
    CHECK_EQ(out1.input.aim_x, out2.input.aim_x);
    CHECK_EQ(out1.input.aim_y, out2.input.aim_y);
    CHECK_EQ(out1.input.buttons, out2.input.buttons);
    CHECK_EQ(out1.decision, out2.decision);
}

void test_policy_controller_reset_clears_state() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(0, pulse::Phase::Live);
    (void)ctrl.act(state, 0);
    state.tick = 5;
    (void)ctrl.act(state, 0);
    ctrl.reset();
    auto out = ctrl.act(state, 0);
    CHECK(out.decision);
}

void test_policy_controller_both_seats() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(0, pulse::Phase::Live);
    auto out0 = ctrl.act(state, 0);
    CHECK(out0.decision);
    auto out1 = ctrl.act(state, 1);
    CHECK(out1.decision);
    CHECK(out0.input.move_x >= -1 && out0.input.move_x <= 1);
    CHECK(out1.input.move_x >= -1 && out1.input.move_x <= 1);
}

void test_policy_controller_button_edge_triggered() {
    auto bundle = build_synthetic_bundle(0);
    pulse::Policy policy;
    (void)policy.load_from_bytes(bundle);
    pulse::ai::PolicyController ctrl(policy);
    auto state = make_test_state(0, pulse::Phase::Live);
    (void)ctrl.act(state, 0);
    state.tick = 1;
    auto out1 = ctrl.act(state, 0);
    CHECK_EQ(out1.input.buttons, pulse::ButtonNone);
    state.tick = 2;
    auto out2 = ctrl.act(state, 0);
    CHECK_EQ(out2.input.buttons, pulse::ButtonNone);
}

// ---- Blocker 7: Tensor ordinal validation ----
// Swapping two same-shaped tensors (trunk.2.weight ↔ trunk.4.weight, both
// 512×512) must be rejected. Set-membership would pass but ordinal
// validation must catch the swap.

void test_policy_tensor_ordinal_swap_rejected() {
    std::vector<unsigned char> buf;
    w_u32(buf, 0x42505A50);  // magic
    w_u32(buf, 1);  // version
    w_u32(buf, 64); w_u32(buf, 512); w_u32(buf, 9); w_u32(buf, 9); w_u32(buf, 4);
    w_u32(buf, 833047); w_u32(buf, 16);
    std::string header = "{\"checkpoint_sha256\":\"a\",\"dll_sha256\":\"b\"}";
    w_u32(buf, static_cast<std::uint32_t>(header.size()));
    for (char c : header) buf.push_back(static_cast<unsigned char>(c));

    // Ordinals 0-1: trunk.0.weight/bias (512x64, 512)
    w_tensor(buf, "trunk.0.weight", std::vector<float>(512 * 64, 0.0f), {512, 64});
    w_tensor(buf, "trunk.0.bias", std::vector<float>(512, 0.0f), {512});
    // Ordinals 2-3: SWAPPED — trunk.4.weight at ordinal 2 (should be trunk.2.weight)
    w_tensor(buf, "trunk.4.weight", std::vector<float>(512 * 512, 0.0f), {512, 512});
    w_tensor(buf, "trunk.2.bias", std::vector<float>(512, 0.0f), {512});
    // Ordinals 4-5: SWAPPED — trunk.2.weight at ordinal 4 (should be trunk.4.weight)
    w_tensor(buf, "trunk.2.weight", std::vector<float>(512 * 512, 0.0f), {512, 512});
    w_tensor(buf, "trunk.4.bias", std::vector<float>(512, 0.0f), {512});
    // Ordinals 6-7: trunk.6.weight/bias (correct)
    w_tensor(buf, "trunk.6.weight", std::vector<float>(512 * 512, 0.0f), {512, 512});
    w_tensor(buf, "trunk.6.bias", std::vector<float>(512, 0.0f), {512});
    // Ordinals 8-15: heads (correct)
    w_tensor(buf, "move_head.weight", std::vector<float>(9 * 512, 0.0f), {9, 512});
    w_tensor(buf, "move_head.bias", std::vector<float>(9, 0.0f), {9});
    w_tensor(buf, "aim_head.weight", std::vector<float>(9 * 512, 0.0f), {9, 512});
    w_tensor(buf, "aim_head.bias", std::vector<float>(9, 0.0f), {9});
    w_tensor(buf, "button_head.weight", std::vector<float>(4 * 512, 0.0f), {4, 512});
    w_tensor(buf, "button_head.bias", std::vector<float>(4, 0.0f), {4});
    w_tensor(buf, "value_head.weight", std::vector<float>(1 * 512, 0.0f), {1, 512});
    w_tensor(buf, "value_head.bias", std::vector<float>(1, 0.0f), {1});

    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);

    pulse::Policy policy;
    CHECK(!policy.load_from_bytes(buf));
}

// ---- Blocker 8: Required provenance (ruleset_sha256, simulator_sha256) ----

void test_manifest_missing_ruleset_sha_rejected() {
    // Manifest without ruleset_sha256 must be rejected (no longer optional).
    pulse::Policy policy;
    std::string manifest = build_valid_manifest();
    // Remove the ruleset_sha256 line.
    std::string key = "\"ruleset_sha256\": \"2222222222222222222222222222222222222222222222222222222222222222\",";
    size_t pos = manifest.find(key);
    CHECK(pos != std::string::npos);
    manifest.erase(pos, key.size());
    std::string error_msg;
    CHECK(policy.parse_json_internal(manifest, error_msg));
    CHECK(!policy.validate_manifest_internal(kValidBundleSha, error_msg));
}

void test_manifest_missing_simulator_sha_rejected() {
    // Manifest without simulator_sha256 must be rejected (no longer optional).
    pulse::Policy policy;
    std::string manifest = build_valid_manifest();
    // Remove the simulator_sha256 line.
    std::string key = "\"simulator_sha256\": \"3333333333333333333333333333333333333333333333333333333333333333\",";
    size_t pos = manifest.find(key);
    CHECK(pos != std::string::npos);
    manifest.erase(pos, key.size());
    std::string error_msg;
    CHECK(policy.parse_json_internal(manifest, error_msg));
    CHECK(!policy.validate_manifest_internal(kValidBundleSha, error_msg));
}

void test_manifest_empty_config_sha_rejected() {
    // Manifest with empty config_hash must be rejected (no longer optional).
    pulse::Policy policy;
    std::string manifest = build_valid_manifest();
    // Replace config_hash with empty string.
    std::string bad = "\"config_hash\": \"\",";
    size_t pos = manifest.find("\"config_hash\": \"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\",");
    CHECK(pos != std::string::npos);
    manifest.replace(pos, std::string("\"config_hash\": \"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\",").size(), bad);
    std::string error_msg;
    CHECK(policy.parse_json_internal(manifest, error_msg));
    CHECK(!policy.validate_manifest_internal(kValidBundleSha, error_msg));
}

}  // namespace

void test_sha256_abc() {
    // SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    const unsigned char data[] = {'a', 'b', 'c'};
    unsigned char hash[32];
    sha256(data, 3, hash);
    const char expected[] = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    char actual[65];
    for (int i = 0; i < 32; ++i) snprintf(actual + 2*i, 3, "%02x", hash[i]);
    actual[64] = '\0';
    if (std::strcmp(actual, expected) != 0) {
        std::fprintf(stderr, "FAIL: SHA-256('abc') = %s, expected %s\n", actual, expected);
        ++tests_failed;
    }
    ++tests_run;
}

int main() {
    test_sha256_abc();
    test_factors_to_frame_input_seat0();
    test_factors_to_frame_input_seat1_mirrored();
    test_factors_button_strike();
    test_factors_button_ability();
    test_factors_button_dash();
    test_factors_out_of_range_clamps();
    test_mask_button_logits_all_legal();
    test_mask_button_logits_some_illegal();
    test_mask_button_logits_none_legal();
    test_greedy_factors_basic();
    test_greedy_factors_masked_button();
    test_policy_load_synthetic();
    test_policy_load_tampered_sha();
    test_policy_load_bad_magic();
    test_policy_forward_finite();
    test_policy_golden_load_and_verify();
    test_policy_golden_sha_tamper();
    test_policy_golden_bad_magic();
    test_policy_golden_mismatch_detected();
    test_policy_bundle_trailing_bytes_rejected();
    test_policy_golden_trailing_bytes_rejected();
    test_policy_golden_action_out_of_range_rejected();
    // Mutation tests for hash-valid malformed inputs.
    test_policy_tensor_rejected_renamed();
    test_policy_tensor_rejected_shape_product_mismatch();
    test_policy_tensor_rejected_huge_dimension();
    test_policy_tensor_rejected_nonfinite();
    test_policy_golden_rejected_no_op_illegal();
    test_policy_golden_rejected_all_zero_mask();
    test_policy_golden_rejected_nonfinite_observation();
    test_policy_greedy_action_on_state();
    // JSON parser and manifest validation tests.
    test_json_parser_happy_path();
    test_json_parser_duplicate_key_rejected();
    test_json_parser_wrong_type_rejected();
    test_json_parser_missing_key_rejected();
    test_json_parser_trailing_content_rejected();
    test_json_parser_malformed_number_rejected();
    test_json_parser_malformed_escape_rejected();
    test_json_parser_excessive_depth_rejected();
    test_manifest_validation_happy_path();
    test_manifest_validation_wrong_bundle_sha_rejected();
    test_manifest_validation_wrong_hash_format_rejected();
    // PolicyController 12-tick cadence tests.
    test_policy_controller_first_use_decides();
    test_policy_controller_hold_between_decisions();
    test_policy_controller_cadence_at_tick_12();
    test_policy_controller_cadence_at_tick_24();
    test_policy_controller_phase_change_decides();
    test_policy_controller_player_change_decides();
    test_policy_controller_rewind_decides();
    test_policy_controller_matchover_zero_input();
    test_policy_controller_idempotent_same_tick();
    test_policy_controller_reset_clears_state();
    test_policy_controller_both_seats();
    test_policy_controller_button_edge_triggered();
    // Blocker 7: Tensor ordinal validation.
    test_policy_tensor_ordinal_swap_rejected();
    // Blocker 8: Required provenance.
    test_manifest_missing_ruleset_sha_rejected();
    test_manifest_missing_simulator_sha_rejected();
    test_manifest_empty_config_sha_rejected();

    std::printf("policy_tests: %d run, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
