// TEST/DEV ONLY -- NON-CERTIFIED. See pulse_policy_devgen.hpp for details.

#include "pulse_policy_devgen.hpp"

#include <cstring>

namespace pulse::devgen {

void w_u32(std::vector<unsigned char>& buf, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}
void w_u16(std::vector<unsigned char>& buf, std::uint16_t v) {
    for (int i = 0; i < 2; ++i) buf.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}
void w_u64(std::vector<unsigned char>& buf, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
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

// Minimal SHA-256 (standalone copy; matches pulse_policy.cpp's internal one
// but kept independent so this dev-only header has no production dependency).
void sha256(const unsigned char* data, std::size_t len, unsigned char out[32]) {
    static const std::uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](std::uint32_t x, int n) -> std::uint32_t {
        return (x >> n) | (x << (32 - n));
    };
    std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8;
    std::vector<unsigned char> msg(data, data + len);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i)
        msg.push_back(static_cast<unsigned char>((bit_len >> (8 * i)) & 0xFF));
    for (std::size_t blk = 0; blk < msg.size(); blk += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[blk + 4 * i]) << 24) |
                   (static_cast<std::uint32_t>(msg[blk + 4 * i + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[blk + 4 * i + 2]) << 8) |
                   (static_cast<std::uint32_t>(msg[blk + 4 * i + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; ++i) {
        out[4 * i] = (h[i] >> 24) & 0xFF;
        out[4 * i + 1] = (h[i] >> 16) & 0xFF;
        out[4 * i + 2] = (h[i] >> 8) & 0xFF;
        out[4 * i + 3] = h[i] & 0xFF;
    }
}

float pattern_weight(std::size_t index) {
    // Fixed index-hash (splitmix-style avalanche), fully deterministic.
    std::uint32_t h = static_cast<std::uint32_t>(index) * 2654435761u;
    h ^= h >> 15;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    // Map to roughly [-0.01, 0.01].
    double frac = static_cast<double>(h % 20001u) / 20000.0;  // [0, 1]
    return static_cast<float>((frac - 0.5) * 0.02);
}

std::vector<unsigned char> build_synthetic_bundle(std::size_t golden_count, WeightPattern pattern) {
    (void)golden_count;  // Bundle no longer contains golden cases (phase 3).
    std::vector<unsigned char> buf;
    // Fixed header (9 u32 fields, no golden_count).
    w_u32(buf, 0x42505A50);  // "PZPB" LE
    w_u32(buf, 1);           // version
    w_u32(buf, 64);          // obs_size
    w_u32(buf, 512);         // hidden
    w_u32(buf, 9);           // move
    w_u32(buf, 9);           // aim
    w_u32(buf, 4);           // button
    w_u32(buf, 833047);      // param_count
    w_u32(buf, 16);          // tensor_count

    // Header JSON (minimal).
    std::string header = "{\"checkpoint_sha256\":\"a\",\"dll_sha256\":\"b\"}";
    w_u32(buf, static_cast<std::uint32_t>(header.size()));
    for (char c : header) buf.push_back(static_cast<unsigned char>(c));

    // Running flat-parameter index for deterministic patterned weights (spans
    // all tensors in emission order, weights and biases alike).
    std::size_t flat_index = 0;
    auto make_values = [&](std::size_t count) {
        std::vector<float> values(count, 0.0f);
        if (pattern == WeightPattern::Patterned) {
            for (std::size_t i = 0; i < count; ++i) values[i] = pattern_weight(flat_index + i);
        }
        flat_index += count;
        return values;
    };

    auto lin_w = [&](const std::string& name, int out, int in) {
        auto values = make_values(static_cast<std::size_t>(out) * static_cast<std::size_t>(in));
        w_tensor(buf, name, values,
                 {static_cast<std::uint32_t>(out), static_cast<std::uint32_t>(in)});
    };
    auto lin_b = [&](const std::string& name, int out) {
        auto values = make_values(static_cast<std::size_t>(out));
        w_tensor(buf, name, values, {static_cast<std::uint32_t>(out)});
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

std::vector<unsigned char> build_synthetic_golden(std::size_t case_count) {
    std::vector<unsigned char> buf;
    w_u32(buf, 0x46475A50);  // "PZGF" LE
    w_u32(buf, 1);           // version
    w_u32(buf, static_cast<std::uint32_t>(case_count));
    w_u32(buf, 64);  // obs_size
    w_u32(buf, 9);   // move
    w_u32(buf, 9);   // aim
    w_u32(buf, 4);   // button

    for (std::size_t i = 0; i < case_count; ++i) {
        w_u64(buf, 1000001 + i);  // seed
        buf.push_back(0);         // player_index
        buf.push_back(0);         // learner_seat
        buf.push_back(0);         // char0
        buf.push_back(0);         // char1
        buf.push_back(1);         // opponent_kind (int8)
        w_u32(buf, 0);            // sim_tick
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

std::vector<unsigned char> build_synthetic_golden_from_cases(const std::vector<DevGoldenCase>& cases) {
    std::vector<unsigned char> buf;
    w_u32(buf, 0x46475A50);  // "PZGF" LE
    w_u32(buf, 1);           // version
    w_u32(buf, static_cast<std::uint32_t>(cases.size()));
    w_u32(buf, 64);  // obs_size
    w_u32(buf, 9);   // move
    w_u32(buf, 9);   // aim
    w_u32(buf, 4);   // button

    for (const auto& gc : cases) {
        w_u64(buf, gc.seed);
        buf.push_back(gc.player_index);
        buf.push_back(gc.learner_seat);
        buf.push_back(gc.char0);
        buf.push_back(gc.char1);
        buf.push_back(static_cast<unsigned char>(gc.opponent_kind));
        w_u32(buf, gc.sim_tick);
        for (float v : gc.observation) w_f32(buf, v);
        for (auto m : gc.button_mask) buf.push_back(m);
        for (float v : gc.move_logits) w_f32(buf, v);
        for (float v : gc.aim_logits) w_f32(buf, v);
        for (float v : gc.button_logits) w_f32(buf, v);
        w_f32(buf, gc.value);
        buf.push_back(gc.expected_move);
        buf.push_back(gc.expected_aim);
        buf.push_back(gc.expected_button);
    }

    unsigned char hash[32];
    sha256(buf.data(), buf.size(), hash);
    for (int i = 0; i < 32; ++i) buf.push_back(hash[i]);

    return buf;
}

}  // namespace pulse::devgen
