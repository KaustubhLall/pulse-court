#include "pulse_policy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace pulse {

namespace {

// Little-endian readers.
template <typename T>
T read_le(const unsigned char* p) noexcept {
    T v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        v |= static_cast<T>(p[i]) << (8 * i);
    }
    return v;
}

// SHA-256 implementation (FIPS 180-4)
void sha256(const unsigned char* data, std::size_t len, unsigned char* out_hash) noexcept {
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
    std::uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19,
    };
    auto rotr = [](std::uint32_t x, int n) -> std::uint32_t {
        return (x >> n) | (x << (32 - n));
    };
    std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8;
    std::vector<unsigned char> msg(data, data + len);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<unsigned char>((bit_len >> (8 * i)) & 0xFF));
    }
    for (std::size_t blk = 0; blk < msg.size(); blk += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[blk + 4*i]) << 24) |
                   (static_cast<std::uint32_t>(msg[blk + 4*i + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[blk + 4*i + 2]) << 8) |
                   (static_cast<std::uint32_t>(msg[blk + 4*i + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            std::uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t temp1 = hh + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = S0 + maj;
            hh=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    for (int i = 0; i < 8; ++i) {
        out_hash[4*i]   = (h[i] >> 24) & 0xFF;
        out_hash[4*i+1] = (h[i] >> 16) & 0xFF;
        out_hash[4*i+2] = (h[i] >> 8) & 0xFF;
        out_hash[4*i+3] = h[i] & 0xFF;
    }
}

std::uint32_t rd_u32(const unsigned char* p) noexcept { return read_le<std::uint32_t>(p); }
std::uint64_t rd_u64(const unsigned char* p) noexcept { return read_le<std::uint64_t>(p); }
std::uint16_t rd_u16(const unsigned char* p) noexcept { return read_le<std::uint16_t>(p); }

float rd_f32(const unsigned char* p) noexcept {
    std::uint32_t bits = rd_u32(p);
    float f;
    std::memcpy(&f, &bits, sizeof(float));
    return f;
}

std::string hex_string(const unsigned char* data, std::size_t n) {
    static const char hex[] = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (std::size_t i = 0; i < n; ++i) {
        s[2 * i] = hex[(data[i] >> 4) & 0xF];
        s[2 * i + 1] = hex[data[i] & 0xF];
    }
    return s;
}

bool is_finite(float v) noexcept {
    return !std::isnan(v) && !std::isinf(v);
}

}  // namespace

// SHA-256 hex helper (outside anonymous namespace for class method access)
std::string sha256_hex(const unsigned char* data, std::size_t len) {
    unsigned char hash[32];
    sha256(data, len, hash);
    return hex_string(hash, 32);
}

void Policy::linear(const float* input, const float* weight,
                    const float* bias, float* output,
                    std::size_t in_features, std::size_t out_features) noexcept {
    // PyTorch Linear: output = input @ weight^T + bias
    // weight is [out_features, in_features] (row-major).
    for (std::size_t o = 0; o < out_features; ++o) {
        float sum = bias[o];
        const float* w_row = weight + o * in_features;
        for (std::size_t i = 0; i < in_features; ++i) {
            sum += input[i] * w_row[i];
        }
        output[o] = sum;
    }
}

void Policy::relu(float* x, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        if (x[i] < 0.0f) x[i] = 0.0f;
    }
}

bool Policy::parse_tensor(const unsigned char* data, std::size_t size,
                          std::size_t& offset,
                          std::size_t& ordinal,
                          std::vector<float>& out,
                          std::vector<std::uint32_t>& shape) noexcept {
    if (offset + 2 > size) return false;
    std::uint16_t name_len = rd_u16(data + offset);
    offset += 2;
    if (offset + name_len > size) return false;

    // Read and validate tensor name
    std::string name(data + offset, data + offset + name_len);
    offset += name_len;

    // Canonical tensor names in exact ordinal order. Tensors are consumed
    // positionally, so the name at each ordinal must match exactly — set
    // membership is insufficient (a swap of two same-shaped entries would
    // pass set membership but corrupt the model).
    static const std::array<const char*, 16> kCanonicalTensorNames = {
        "trunk.0.weight", "trunk.0.bias",
        "trunk.2.weight", "trunk.2.bias",
        "trunk.4.weight", "trunk.4.bias",
        "trunk.6.weight", "trunk.6.bias",
        "move_head.weight", "move_head.bias",
        "aim_head.weight", "aim_head.bias",
        "button_head.weight", "button_head.bias",
        "value_head.weight", "value_head.bias"
    };

    // Validate exact name at exact ordinal.
    if (ordinal >= kCanonicalTensorNames.size()) {
        std::fprintf(stderr, "parse_tensor: ordinal %zu out of range\n", ordinal);
        return false;
    }
    if (name != kCanonicalTensorNames[ordinal]) {
        std::fprintf(stderr, "parse_tensor: name %s at ordinal %zu != expected %s\n",
                     name.c_str(), ordinal, kCanonicalTensorNames[ordinal]);
        return false;
    }
    ++ordinal;
    
    if (offset + 4 > size) return false;
    std::uint32_t ndim = rd_u32(data + offset);
    offset += 4;
    // Hard cap on ndim to prevent OOB
    if (ndim > 4) return false;
    shape.clear();
    std::uint64_t expected_elem = 1;
    for (std::uint32_t d = 0; d < ndim; ++d) {
        if (offset + 4 > size) return false;
        std::uint32_t dim = rd_u32(data + offset);
        offset += 4;
        // Hard cap on dimension size
        if (dim > 10000) return false;
        shape.push_back(dim);
        expected_elem *= dim;
        // Check for overflow
        if (expected_elem > 1000000000ULL) return false;
    }
    if (offset + 8 > size) return false;
    std::uint64_t elem_count = rd_u64(data + offset);
    offset += 8;
    // Validate elem_count matches shape product
    if (elem_count != expected_elem) return false;
    // Hard cap on element count
    if (elem_count > 1000000000ULL) return false;
    if (offset + elem_count * 4 > size) return false;
    try {
        out.resize(static_cast<std::size_t>(elem_count));
    } catch (...) {
        return false;  // allocation failed
    }
    for (std::uint64_t i = 0; i < elem_count; ++i) {
        out[static_cast<std::size_t>(i)] = rd_f32(data + offset + i * 4);
        // Check for nonfinite values
        if (!is_finite(out[static_cast<std::size_t>(i)])) return false;
    }
    offset += static_cast<std::size_t>(elem_count) * 4;
    return true;
}



bool Policy::load_from_bytes(const std::vector<unsigned char>& data) noexcept {
    loaded_ = false;
    const std::size_t size = data.size();

    // Minimum: magic(4) + 10*u32(40) + header_json(4) + tensors + sha(32)
    if (size < 4 + 40 + 32) return false;

    // Verify SHA-256 of all bytes except the last 32 (embedded suffix check).
    std::vector<unsigned char> body(data.begin(), data.end() - 32);
    unsigned char hash[32];
    sha256(body.data(), body.size(), hash);
    // Compare with embedded SHA-256.
    if (std::memcmp(hash, data.data() + size - 32, 32) != 0) {
        std::fprintf(stderr, "load_from_bytes: SHA-256 mismatch\n");
        return false;  // SHA-256 mismatch
    }

    // Store bundle SHA-256 as hex string (SHA-256 of the ENTIRE file, including the embedded suffix).
    {
        unsigned char full_hash[32];
        sha256(data.data(), data.size(), full_hash);
        static const char hex[] = "0123456789abcdef";
        bundle_sha256_.resize(64);
        for (int i = 0; i < 32; ++i) {
            bundle_sha256_[2*i]   = hex[(full_hash[i] >> 4) & 0xF];
            bundle_sha256_[2*i+1] = hex[full_hash[i] & 0xF];
        }
    }

    // Parse fixed header (10 u32 fields in the new format).
    std::size_t off = 0;
    if (size < 4) return false;
    // Magic
    std::uint32_t magic = rd_u32(data.data());
    if (magic != kPolicyBundleMagic) {
        std::fprintf(stderr, "load_from_bytes: magic mismatch: 0x%08x != 0x%08x\n", magic, kPolicyBundleMagic);
        return false;
    }
    off += 4;
    version_ = rd_u32(data.data() + off); off += 4;
    if (version_ != kPolicyBundleVersion) {
        std::fprintf(stderr, "load_from_bytes: version mismatch: %u != %u\n", version_, kPolicyBundleVersion);
        return false;
    }
    std::uint32_t obs_size = rd_u32(data.data() + off); off += 4;
    std::uint32_t hidden = rd_u32(data.data() + off); off += 4;
    std::uint32_t move_n = rd_u32(data.data() + off); off += 4;
    std::uint32_t aim_n = rd_u32(data.data() + off); off += 4;
    std::uint32_t btn_n = rd_u32(data.data() + off); off += 4;
    std::uint32_t param_n = rd_u32(data.data() + off); off += 4;
    std::uint32_t tensor_n = rd_u32(data.data() + off); off += 4;
    // New format: 10 u32 fields total, with header_size as the 10th
    std::uint32_t header_size = rd_u32(data.data() + off); off += 4;

    if (obs_size != kPolicyObsSize) return false;
    if (hidden != kPolicyHiddenWidth) return false;
    if (move_n != kPolicyMoveCount) return false;
    if (aim_n != kPolicyAimCount) return false;
    if (btn_n != kPolicyButtonCount) return false;
    if (param_n != kPolicyParamCount) return false;
    if (tensor_n != kPolicyTensorCount) return false;

    // Skip header JSON.
    off += header_size;

    // Parse tensors in canonical order with strict ordinal validation.
    std::size_t tensor_ordinal = 0;
    auto parse_linear = [&](LinearLayer& layer) -> bool {
        std::vector<float> w; std::vector<std::uint32_t> w_shape;
        if (!parse_tensor(data.data(), size, off, tensor_ordinal, w, w_shape)) return false;
        std::vector<float> b; std::vector<std::uint32_t> b_shape;
        if (!parse_tensor(data.data(), size, off, tensor_ordinal, b, b_shape)) return false;
        if (w_shape.size() != 2) return false;
        layer.out_features = w_shape[0];
        layer.in_features = w_shape[1];
        if (b_shape.size() != 1 || b_shape[0] != layer.out_features) return false;
        for (float v : w) if (!is_finite(v)) return false;
        for (float v : b) if (!is_finite(v)) return false;
        layer.weight = std::move(w);
        layer.bias = std::move(b);
        return true;
    };

    for (std::size_t i = 0; i < 4; ++i) {
        if (!parse_linear(trunk_[i])) return false;
    }
    if (!parse_linear(move_head_)) return false;
    if (!parse_linear(aim_head_)) return false;
    if (!parse_linear(button_head_)) return false;
    if (!parse_linear(value_head_)) return false;

    // All 16 tensors must have been consumed exactly.
    if (tensor_ordinal != kPolicyTensorCount) return false;

    // Validate dimensions.
    if (trunk_[0].in_features != kPolicyObsSize) return false;
    if (trunk_[0].out_features != kPolicyHiddenWidth) return false;
    for (std::size_t i = 1; i < 4; ++i) {
        if (trunk_[i].in_features != kPolicyHiddenWidth) return false;
        if (trunk_[i].out_features != kPolicyHiddenWidth) return false;
    }
    if (move_head_.in_features != kPolicyHiddenWidth || move_head_.out_features != kPolicyMoveCount) return false;
    if (aim_head_.in_features != kPolicyHiddenWidth || aim_head_.out_features != kPolicyAimCount) return false;
    if (button_head_.in_features != kPolicyHiddenWidth || button_head_.out_features != kPolicyButtonCount) return false;
    if (value_head_.in_features != kPolicyHiddenWidth || value_head_.out_features != 1) return false;

    param_count_ = param_n;

    // Strict EOF: no trailing bytes between the last tensor and the SHA-256.
    // The body is data[:-32], so off must equal body.size() (= size - 32).
    if (off != size - 32) {
        return false;  // trailing bytes detected
    }

    // Extract checkpoint/dll SHA from header JSON.
    {
        std::string header_json(reinterpret_cast<const char*>(data.data() + 4 + 40),
                                header_size);
        auto extract_hex = [&](const std::string& key) -> std::string {
            std::size_t pos = header_json.find(key);
            if (pos == std::string::npos) return "";
            pos = header_json.find(':', pos);
            if (pos == std::string::npos) return "";
            pos = header_json.find('"', pos + 1);
            if (pos == std::string::npos) return "";
            std::size_t end = header_json.find('"', pos + 1);
            if (end == std::string::npos) return "";
            return header_json.substr(pos + 1, end - pos - 1);
        };
        checkpoint_sha256_ = extract_hex("checkpoint_sha256");
        dll_sha256_ = extract_hex("dll_sha256");
    }

    loaded_ = true;
    return true;
}

PolicyOutput Policy::forward(const std::array<float, kPolicyObsSize>& obs) const noexcept {
    PolicyOutput out{};
    if (!loaded_) return out;

    // Trunk: 4 Linear + ReLU layers.
    std::array<float, kPolicyHiddenWidth> h0, h1, h2, h3;
    linear(obs.data(), trunk_[0].weight.data(), trunk_[0].bias.data(),
           h0.data(), trunk_[0].in_features, trunk_[0].out_features);
    relu(h0.data(), kPolicyHiddenWidth);
    linear(h0.data(), trunk_[1].weight.data(), trunk_[1].bias.data(),
           h1.data(), trunk_[1].in_features, trunk_[1].out_features);
    relu(h1.data(), kPolicyHiddenWidth);
    linear(h1.data(), trunk_[2].weight.data(), trunk_[2].bias.data(),
           h2.data(), trunk_[2].in_features, trunk_[2].out_features);
    relu(h2.data(), kPolicyHiddenWidth);
    linear(h2.data(), trunk_[3].weight.data(), trunk_[3].bias.data(),
           h3.data(), trunk_[3].in_features, trunk_[3].out_features);
    relu(h3.data(), kPolicyHiddenWidth);

    // Heads.
    linear(h3.data(), move_head_.weight.data(), move_head_.bias.data(),
           out.move_logits.data(), move_head_.in_features, move_head_.out_features);
    linear(h3.data(), aim_head_.weight.data(), aim_head_.bias.data(),
           out.aim_logits.data(), aim_head_.in_features, aim_head_.out_features);
    linear(h3.data(), button_head_.weight.data(), button_head_.bias.data(),
           out.button_logits.data(), button_head_.in_features, button_head_.out_features);

    float raw_value = 0.0f;
    linear(h3.data(), value_head_.weight.data(), value_head_.bias.data(),
           &raw_value, value_head_.in_features, value_head_.out_features);
    out.value = std::tanh(raw_value);

    return out;
}

std::array<std::int8_t, 3>
Policy::greedy_action(const GameState& state, std::size_t player_index) const noexcept {
    if (!loaded_) return {0, 0, 0};
    ai::Observation obs = ai::encode_observation(state, player_index);
    ai::ActionMask mask = ai::make_action_mask(state, player_index);
    PolicyOutput out = forward(obs);
    std::array<float, 9> move_l, aim_l;
    std::array<float, 4> btn_l;
    std::array<std::uint8_t, 4> btn_m;
    std::copy(out.move_logits.begin(), out.move_logits.end(), move_l.begin());
    std::copy(out.aim_logits.begin(), out.aim_logits.end(), aim_l.begin());
    std::copy(out.button_logits.begin(), out.button_logits.end(), btn_l.begin());
    std::copy(mask.button.begin(), mask.button.end(), btn_m.begin());
    return ai::greedy_factors_from_logits(move_l, aim_l, btn_l, btn_m);
}

FrameInput
Policy::greedy_frame_input(const GameState& state, std::size_t player_index) const noexcept {
    auto factors = greedy_action(state, player_index);
    return ai::factors_to_frame_input(factors[0], factors[1], factors[2], player_index);
}

std::size_t Policy::verify_golden_cases(float logit_tolerance) const noexcept {
    if (!loaded_) return golden_cases_.size();
    std::size_t mismatches = 0;
    for (const auto& gc : golden_cases_) {
        PolicyOutput out = forward(gc.observation);
        std::array<float, 9> move_l, aim_l;
        std::array<float, 4> btn_l;
        std::copy(out.move_logits.begin(), out.move_logits.end(), move_l.begin());
        std::copy(out.aim_logits.begin(), out.aim_logits.end(), aim_l.begin());
        std::copy(out.button_logits.begin(), out.button_logits.end(), btn_l.begin());
        auto factors = ai::greedy_factors_from_logits(move_l, aim_l, btn_l, gc.button_mask);
        if (static_cast<std::uint8_t>(factors[0]) != gc.expected_move ||
            static_cast<std::uint8_t>(factors[1]) != gc.expected_aim ||
            static_cast<std::uint8_t>(factors[2]) != gc.expected_button) {
            ++mismatches;
            continue;
        }
        // Also verify stored logits match the forward pass within tolerance.
        for (std::size_t i = 0; i < kPolicyMoveCount; ++i) {
            if (std::abs(out.move_logits[i] - gc.move_logits[i]) > logit_tolerance) {
                ++mismatches;
                break;
            }
        }
        for (std::size_t i = 0; i < kPolicyAimCount; ++i) {
            if (std::abs(out.aim_logits[i] - gc.aim_logits[i]) > logit_tolerance) {
                ++mismatches;
                break;
            }
        }
        for (std::size_t i = 0; i < kPolicyButtonCount; ++i) {
            if (std::abs(out.button_logits[i] - gc.button_logits[i]) > logit_tolerance) {
                ++mismatches;
                break;
            }
        }
        if (std::abs(out.value - gc.value) > logit_tolerance) {
            ++mismatches;
        }
    }
    return mismatches;
}

bool Policy::load_golden(const std::string& path) noexcept {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    return load_golden_from_bytes(data);
}

std::string Policy::compute_golden_sha256(const std::string& path) const noexcept {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    if (data.empty()) return "";
    
    unsigned char hash[32];
    sha256(data.data(), data.size(), hash);
    
    static const char hex[] = "0123456789abcdef";
    std::string sha_hex(64, '0');
    for (int i = 0; i < 32; ++i) {
        sha_hex[2*i]   = hex[(hash[i] >> 4) & 0xF];
        sha_hex[2*i+1] = hex[hash[i] & 0xF];
    }
    return sha_hex;
}

bool Policy::validate_golden_against_manifest(const std::string& golden_path,
                                             std::string& error_msg) noexcept {
    if (!parsed_manifest_ || parsed_manifest_->type != JsonType::Object) {
        error_msg = "manifest not loaded";
        return false;
    }
    
    const JsonNode& root = static_cast<const JsonNode&>(*parsed_manifest_);
    
    // Helper to navigate to a path and check type
    auto get_field = [&](const std::string& path, JsonType expected_type) -> const JsonNode* {
        const JsonNode* node = navigate_json(root, path, error_msg);
        if (!node) return nullptr;
        if (node->type != expected_type) {
            error_msg = "Type mismatch for " + path;
            return nullptr;
        }
        return node;
    };
    
    // Helper to get a string field
    auto get_string = [&](const std::string& path) -> std::string {
        const JsonNode* node = get_field(path, JsonType::String);
        if (!node) return "";
        return node->string_val;
    };
    
    // Helper to get an int field
    auto get_int = [&](const std::string& path) -> std::int64_t {
        const JsonNode* node = get_field(path, JsonType::Number);
        if (!node) return -1;
        return node->int_val;
    };
    
    // Compute actual golden file SHA
    std::string actual_golden_sha = compute_golden_sha256(golden_path);
    if (actual_golden_sha.empty()) {
        error_msg = "failed to compute golden file SHA";
        return false;
    }
    
    // Cross-check golden SHA from manifest
    std::string manifest_golden_sha = get_string("golden.sha256");
    if (manifest_golden_sha.empty() || manifest_golden_sha != actual_golden_sha) {
        error_msg = "golden SHA mismatch with manifest";
        return false;
    }
    
    // Cross-check golden filename from manifest
    std::string manifest_golden_filename = get_string("golden.filename");
    if (manifest_golden_filename.empty()) {
        error_msg = "missing golden.filename in manifest";
        return false;
    }
    // Extract filename from path
    std::size_t last_sep = golden_path.find_last_of("/\\");
    std::string actual_filename = (last_sep == std::string::npos) ? 
                                  golden_path : golden_path.substr(last_sep + 1);
    if (actual_filename != manifest_golden_filename) {
        error_msg = "golden filename mismatch with manifest";
        return false;
    }
    
    // Cross-check golden case count from manifest
    std::int64_t manifest_case_count = get_int("golden.case_count");
    if (manifest_case_count < 0) {
        error_msg = "invalid golden.case_count in manifest";
        return false;
    }
    if (static_cast<std::size_t>(manifest_case_count) != golden_case_count()) {
        error_msg = "golden case count mismatch with manifest";
        return false;
    }
    
    return true;
}

bool Policy::load_golden_from_bytes(const std::vector<unsigned char>& data) noexcept {
    const std::size_t size = data.size();
    if (size < 4 + 6 * 4 + 32) return false;  // magic + 6 u32 + sha

    // Verify SHA-256 of all bytes except the last 32 (embedded suffix check).
    std::vector<unsigned char> body(data.begin(), data.end() - 32);
    unsigned char hash[32];
    sha256(body.data(), body.size(), hash);
    if (std::memcmp(hash, data.data() + size - 32, 32) != 0) {
        return false;  // SHA-256 mismatch
    }

    std::size_t off = 0;
    // Magic
    if (rd_u32(data.data()) != kGoldenMagic) return false;
    off += 4;
    // Version
    std::uint32_t ver = rd_u32(data.data() + off); off += 4;
    if (ver != kGoldenVersion) return false;
    std::uint32_t case_count = rd_u32(data.data() + off); off += 4;
    std::uint32_t obs_size = rd_u32(data.data() + off); off += 4;
    std::uint32_t move_n = rd_u32(data.data() + off); off += 4;
    std::uint32_t aim_n = rd_u32(data.data() + off); off += 4;
    std::uint32_t btn_n = rd_u32(data.data() + off); off += 4;

    if (obs_size != kPolicyObsSize) return false;
    if (move_n != kPolicyMoveCount) return false;
    if (aim_n != kPolicyAimCount) return false;
    if (btn_n != kPolicyButtonCount) return false;
    if (case_count == 0) return false;

    golden_cases_.clear();
    golden_cases_.reserve(case_count);

    for (std::uint32_t i = 0; i < case_count; ++i) {
        GoldenCase gc{};
        // seed (u64)
        if (off + 8 > size - 32) return false;
        gc.seed = rd_u64(data.data() + off); off += 8;
        // player_index, learner_seat, char0, char1 (4 bytes)
        if (off + 4 > size - 32) return false;
        gc.player_index = data[off++];
        gc.learner_seat = data[off++];
        gc.char0 = data[off++];
        gc.char1 = data[off++];
        // opponent_kind (int8)
        if (off + 1 > size - 32) return false;
        gc.opponent_kind = static_cast<std::int8_t>(data[off++]);
        // sim_tick (u32)
        if (off + 4 > size - 32) return false;
        gc.sim_tick = rd_u32(data.data() + off); off += 4;
        // observation (64 * f32)
        if (off + kPolicyObsSize * 4 > size - 32) return false;
        for (std::size_t j = 0; j < kPolicyObsSize; ++j) {
            gc.observation[j] = rd_f32(data.data() + off + j * 4);
            if (!is_finite(gc.observation[j])) return false;
        }
        off += kPolicyObsSize * 4;
        // button_mask (4 bytes)
        if (off + kPolicyButtonCount > size - 32) return false;
        for (std::size_t j = 0; j < kPolicyButtonCount; ++j) {
            gc.button_mask[j] = data[off++];
        }
        // Validate button mask: no-op (index 0) must be legal, at least one legal action.
        if (gc.button_mask[0] == 0) return false;  // no-op must be legal
        bool any_legal = false;
        for (std::size_t j = 0; j < kPolicyButtonCount; ++j) {
            if (gc.button_mask[j] != 0) { any_legal = true; break; }
        }
        if (!any_legal) return false;  // at least one legal action required
        // move_logits (9 * f32)
        if (off + kPolicyMoveCount * 4 > size - 32) return false;
        for (std::size_t j = 0; j < kPolicyMoveCount; ++j) {
            gc.move_logits[j] = rd_f32(data.data() + off + j * 4);
            if (!is_finite(gc.move_logits[j])) return false;
        }
        off += kPolicyMoveCount * 4;
        // aim_logits (9 * f32)
        if (off + kPolicyAimCount * 4 > size - 32) return false;
        for (std::size_t j = 0; j < kPolicyAimCount; ++j) {
            gc.aim_logits[j] = rd_f32(data.data() + off + j * 4);
            if (!is_finite(gc.aim_logits[j])) return false;
        }
        off += kPolicyAimCount * 4;
        // button_logits (4 * f32)
        if (off + kPolicyButtonCount * 4 > size - 32) return false;
        for (std::size_t j = 0; j < kPolicyButtonCount; ++j) {
            gc.button_logits[j] = rd_f32(data.data() + off + j * 4);
            if (!is_finite(gc.button_logits[j])) return false;
        }
        off += kPolicyButtonCount * 4;
        // value (f32)
        if (off + 4 > size - 32) return false;
        gc.value = rd_f32(data.data() + off); off += 4;
        if (!is_finite(gc.value)) return false;
        // expected_move, expected_aim, expected_button (3 bytes)
        if (off + 3 > size - 32) return false;
        gc.expected_move = data[off++];
        gc.expected_aim = data[off++];
        gc.expected_button = data[off++];
        // Validate action ranges.
        if (gc.expected_move >= kPolicyMoveCount) return false;
        if (gc.expected_aim >= kPolicyAimCount) return false;
        if (gc.expected_button >= kPolicyButtonCount) return false;

        golden_cases_.push_back(gc);
    }

    // Strict EOF: no trailing bytes.
    if (off != size - 32) {
        return false;
    }

    return true;
}

// ---- Three-artifact production load with manifest validation ----

bool Policy::load(const std::string& bundle_path,
                  const std::string& manifest_path) noexcept {
    loaded_ = false;
    provenance_.clear();  // Clear provenance on any load attempt
    std::string error_msg;

    // 1. Load manifest JSON.
    std::ifstream manifest_file(manifest_path);
    if (!manifest_file) {
        std::fprintf(stderr, "load: manifest file not found: %s\n", manifest_path.c_str());
        return false;  // manifest file not found
    }
    std::string manifest_json((std::istreambuf_iterator<char>(manifest_file)),
                              std::istreambuf_iterator<char>());

    // 2. Parse JSON (bounded parser, no external dependency).
    if (!parse_json(manifest_json, error_msg)) {
        std::fprintf(stderr, "load: invalid JSON: %s\n", error_msg.c_str());
        return false;  // invalid JSON
    }

    // 3. Load bundle to get its SHA.
    std::ifstream bundle_file(bundle_path, std::ios::binary);
    if (!bundle_file) {
        std::fprintf(stderr, "load: bundle file not found: %s\n", bundle_path.c_str());
        return false;  // bundle file not found
    }
    std::vector<unsigned char> bundle_data((std::istreambuf_iterator<char>(bundle_file)),
                                           std::istreambuf_iterator<char>());

    // 4. Validate bundle with existing loader.
    if (!load_from_bytes(bundle_data)) {
        std::fprintf(stderr, "load: bundle validation failed\n");
        return false;  // bundle validation failed
    }

    // 5. Validate manifest against bundle SHA and compiled constants.
    if (!validate_manifest(bundle_sha256_, error_msg)) {
        std::fprintf(stderr, "load: manifest validation failed: %s\n", error_msg.c_str());
        loaded_ = false;  // manifest validation failed
        provenance_.clear();  // Clear provenance on validation failure
        return false;
    }

    // 6. Populate provenance snapshot from validated manifest
    if (!populate_provenance(manifest_json, bundle_sha256_, error_msg)) {
        std::fprintf(stderr, "load: provenance population failed: %s\n", error_msg.c_str());
        loaded_ = false;
        provenance_.clear();
        return false;
    }

    // NOTE: Golden file is loaded separately by the verifier, not by load().
    // This allows the verifier to override the golden path for testing.

    return true;
}

bool Policy::load_preview(const std::string& bundle_path) noexcept {
    std::ifstream f(bundle_path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    return load_from_bytes(data);
}

// ---- Bounded JSON parser for manifest validation ----

// Parse a JSON string with escape sequences
static bool parse_json_string(const std::string& json_str, std::size_t& pos,
                       std::string& out, std::string& error_msg,
                       std::size_t /*max_depth*/ = 100) {
    if (pos >= json_str.size() || json_str[pos] != '"') {
        error_msg = "Expected '\"' at string start";
        return false;
    }
    ++pos;
    out.clear();
    while (pos < json_str.size() && json_str[pos] != '"') {
        if (json_str[pos] == '\\') {
            ++pos;
            if (pos >= json_str.size()) {
                error_msg = "Unterminated escape sequence";
                return false;
            }
            char c = json_str[pos];
            switch (c) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    // Unicode escape (4 hex digits)
                    if (pos + 4 >= json_str.size()) {
                        error_msg = "Incomplete unicode escape";
                        return false;
                    }
                    std::string hex = json_str.substr(pos + 1, 4);
                    bool valid = true;
                    for (char hc : hex) {
                        if (!((hc >= '0' && hc <= '9') || (hc >= 'a' && hc <= 'f') || (hc >= 'A' && hc <= 'F'))) {
                            valid = false;
                            break;
                        }
                    }
                    if (!valid) {
                        error_msg = "Invalid unicode escape";
                        return false;
                    }
                    // For simplicity, just skip the escape (we don't need full unicode)
                    pos += 4;
                    out += '?';  // placeholder
                    break;
                }
                default:
                    error_msg = "Invalid escape sequence";
                    return false;
            }
        } else {
            // Reject unescaped control characters (except tab, newline, carriage return)
            unsigned char uc = static_cast<unsigned char>(json_str[pos]);
            if (uc < 0x20 && uc != 0x09 && uc != 0x0A && uc != 0x0D) {
                error_msg = "Unescaped control character in string";
                return false;
            }
            out += json_str[pos];
        }
        ++pos;
        if (out.size() > 10000) {  // bound string size
            error_msg = "String too long";
            return false;
        }
    }
    if (pos >= json_str.size()) {
        error_msg = "Unterminated string";
        return false;
    }
    ++pos;  // skip closing quote
    return true;
}

// Parse JSON number
static bool parse_json_number(const std::string& json_str, std::size_t& pos,
                       JsonNode& out, std::string& error_msg) {
    std::size_t start = pos;
    if (pos < json_str.size() && json_str[pos] == '-') ++pos;
    
    // Integer part
    while (pos < json_str.size() && json_str[pos] >= '0' && json_str[pos] <= '9') {
        ++pos;
    }
    
    // Fractional part
    if (pos < json_str.size() && json_str[pos] == '.') {
        ++pos;
        while (pos < json_str.size() && json_str[pos] >= '0' && json_str[pos] <= '9') {
            ++pos;
        }
    }
    
    // Exponent
    if (pos < json_str.size() && (json_str[pos] == 'e' || json_str[pos] == 'E')) {
        ++pos;
        if (pos < json_str.size() && (json_str[pos] == '+' || json_str[pos] == '-')) {
            ++pos;
        }
        while (pos < json_str.size() && json_str[pos] >= '0' && json_str[pos] <= '9') {
            ++pos;
        }
    }
    
    if (pos == start) {
        error_msg = "Invalid number";
        return false;
    }
    
    std::string num_str = json_str.substr(start, pos - start);
    try {
        // Try parsing as integer first
        if (num_str.find('.') == std::string::npos && 
            num_str.find('e') == std::string::npos && 
            num_str.find('E') == std::string::npos) {
            out.int_val = std::stoll(num_str);
            out.type = JsonType::Number;
        } else {
            out.float_val = std::stod(num_str);
            out.type = JsonType::Number;
        }
    } catch (...) {
        error_msg = "Number parse error";
        return false;
    }
    return true;
}

// Recursive JSON parser with depth limit
static bool parse_json_value(const std::string& json_str, std::size_t& pos,
                      JsonNode& out, std::string& error_msg,
                      std::size_t depth = 0) {
    if (depth > 20) {  // depth limit
        error_msg = "JSON nesting too deep";
        return false;
    }
    
    auto skip_ws = [&]() {
        while (pos < json_str.size() && (json_str[pos] == ' ' || json_str[pos] == '\t' ||
                                         json_str[pos] == '\n' || json_str[pos] == '\r')) {
            ++pos;
        }
    };
    
    skip_ws();
    if (pos >= json_str.size()) {
        error_msg = "Unexpected end of JSON";
        return false;
    }
    
    char c = json_str[pos];
    if (c == '"') {
        out.type = JsonType::String;
        return parse_json_string(json_str, pos, out.string_val, error_msg, depth);
    } else if (c == '{') {
        out.type = JsonType::Object;
        ++pos;
        skip_ws();
        
        while (pos < json_str.size() && json_str[pos] != '}') {
            // Parse key
            if (json_str[pos] != '"') {
                error_msg = "Expected string key in object";
                return false;
            }
            std::string key;
            if (!parse_json_string(json_str, pos, key, error_msg, depth)) {
                return false;
            }
            
            // Check for duplicate keys
            for (const auto& kv : out.object_val) {
                if (kv.first == key) {
                    error_msg = "Duplicate key: " + key;
                    return false;
                }
            }
            
            skip_ws();
            if (pos >= json_str.size() || json_str[pos] != ':') {
                error_msg = "Expected ':' after key";
                return false;
            }
            ++pos;
            
            // Parse value
            JsonNode val;
            if (!parse_json_value(json_str, pos, val, error_msg, depth + 1)) {
                return false;
            }
            
            out.object_val.push_back({key, val});
            skip_ws();
            
            if (pos < json_str.size() && json_str[pos] == ',') {
                ++pos;
                skip_ws();
                // Reject trailing commas
                if (pos < json_str.size() && json_str[pos] == '}') {
                    error_msg = "Trailing comma in object";
                    return false;
                }
            }
        }
        
        if (pos >= json_str.size() || json_str[pos] != '}') {
            error_msg = "Expected '}' to close object";
            return false;
        }
        ++pos;
        return true;
        
    } else if (c == '[') {
        out.type = JsonType::Array;
        ++pos;
        skip_ws();
        
        while (pos < json_str.size() && json_str[pos] != ']') {
            JsonNode elem;
            if (!parse_json_value(json_str, pos, elem, error_msg, depth + 1)) {
                return false;
            }
            out.array_val.push_back(elem);
            if (out.array_val.size() > 10000) {  // array size limit
                error_msg = "Array too large";
                return false;
            }
            skip_ws();
            if (pos < json_str.size() && json_str[pos] == ',') {
                ++pos;
                skip_ws();
                // Reject trailing commas
                if (pos < json_str.size() && json_str[pos] == ']') {
                    error_msg = "Trailing comma in array";
                    return false;
                }
            }
        }
        
        if (pos >= json_str.size() || json_str[pos] != ']') {
            error_msg = "Expected ']' to close array";
            return false;
        }
        ++pos;
        return true;
        
    } else if (c == 't') {
        if (json_str.substr(pos, 4) != "true") {
            error_msg = "Invalid boolean (expected true)";
            return false;
        }
        pos += 4;
        out.type = JsonType::Bool;
        out.bool_val = true;
        return true;
    } else if (c == 'f') {
        if (json_str.substr(pos, 5) != "false") {
            error_msg = "Invalid boolean (expected false)";
            return false;
        }
        pos += 5;
        out.type = JsonType::Bool;
        out.bool_val = false;
        return true;
    } else if (c == 'n') {
        if (json_str.substr(pos, 4) != "null") {
            error_msg = "Invalid null";
            return false;
        }
        pos += 4;
        out.type = JsonType::Null;
        return true;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        return parse_json_number(json_str, pos, out, error_msg);
    } else {
        error_msg = "Unexpected character in JSON";
        return false;
    }
}

// Navigate to a nested key path (e.g., "bundle.sha256")
const JsonNode* Policy::navigate_json(const JsonNode& node, const std::string& path,
                              std::string& error_msg) noexcept {
    const JsonNode* current = &node;
    std::size_t start = 0;
    
    while (start < path.size()) {
        if (current->type != JsonType::Object) {
            error_msg = "Path traversal on non-object at: " + path;
            return nullptr;
        }
        
        std::size_t dot = path.find('.', start);
        if (dot == std::string::npos) dot = path.size();
        
        std::string key = path.substr(start, dot - start);
        const JsonNode* found = nullptr;
        for (const auto& kv : current->object_val) {
            if (kv.first == key) {
                found = &kv.second;
                break;
            }
        }
        
        if (!found) {
            error_msg = "Key not found: " + key;
            return nullptr;
        }
        
        current = found;
        start = dot + 1;
    }
    
    return current;
}

bool Policy::parse_json(const std::string& json_str, std::string& error_msg) noexcept {
    return parse_json_internal(json_str, error_msg);
}

bool Policy::parse_json_internal(const std::string& json_str, std::string& error_msg) noexcept {
    std::size_t pos = 0;
    JsonNode root;
    
    if (!parse_json_value(json_str, pos, root, error_msg)) {
        return false;
    }
    
    // Check for trailing non-whitespace
    auto skip_ws = [&]() {
        while (pos < json_str.size() && (json_str[pos] == ' ' || json_str[pos] == '\t' ||
                                         json_str[pos] == '\n' || json_str[pos] == '\r')) {
            ++pos;
        }
    };
    skip_ws();
    if (pos < json_str.size()) {
        error_msg = "Trailing non-whitespace after JSON";
        return false;
    }
    
    // Store parsed root for validate_manifest to use
    parsed_manifest_ = std::make_unique<JsonNode>();
    *parsed_manifest_ = std::move(root);
    return true;
}

bool Policy::validate_manifest(const std::string& bundle_sha,
                              std::string& error_msg) noexcept {
    // Keep schema validation independently testable under older simulator
    // builds, but never allow strict runtime loading against the v4 bundle
    // contract unless this binary was compiled with the same ruleset.
    if (!validate_manifest_internal(bundle_sha, error_msg)) {
        return false;
    }
    if (kRulesetVersion != 4) {
        error_msg = "compiled ruleset_version " +
                    std::to_string(kRulesetVersion) +
                    " is incompatible with supported policy bundle "
                    "ruleset_version 4";
        return false;
    }
    return true;
}

bool Policy::validate_manifest_internal(const std::string& bundle_sha,
                                      std::string& error_msg) noexcept {
    if (!parsed_manifest_ || parsed_manifest_->type != JsonType::Object) {
        error_msg = "Parsed manifest is not an object";
        return false;
    }
    
    const JsonNode& root = static_cast<const JsonNode&>(*parsed_manifest_);
    
    // Helper to navigate to a path and check type
    auto get_field = [&](const std::string& path, JsonType expected_type) -> const JsonNode* {
        const JsonNode* node = navigate_json(root, path, error_msg);
        if (!node) return nullptr;
        if (node->type != expected_type) {
            error_msg = "Type mismatch for " + path;
            return nullptr;
        }
        return node;
    };
    
    // Helper to get a string field
    auto get_string = [&](const std::string& path) -> std::string {
        const JsonNode* node = get_field(path, JsonType::String);
        if (!node) return "";
        return node->string_val;
    };
    
    // Helper to get an int field
    auto get_int = [&](const std::string& path) -> std::int64_t {
        const JsonNode* node = get_field(path, JsonType::Number);
        if (!node) return -1;
        return node->int_val;
    };
    
    // Helper to get a bool field
    auto get_bool = [&](const std::string& path) -> bool {
        const JsonNode* node = get_field(path, JsonType::Bool);
        if (!node) return false;
        return node->bool_val;
    };
    
    // Validate bundle SHA matches manifest (nested structure: bundle.sha256)
    std::string manifest_bundle_sha = get_string("bundle.sha256");
    if (manifest_bundle_sha.empty() || manifest_bundle_sha != bundle_sha) {
        error_msg = "bundle SHA mismatch";
        return false;
    }
    
    // Validate required hash fields are 64-char lowercase hex.
    // All fields are strictly required — no optional empty fallback for
    // config, ruleset, or simulator fingerprints.
    std::vector<std::string> hash_paths = {
        "bundle.sha256", "golden.sha256", "checkpoint_sha256",
        "config_hash", "reward_contract_hash", "dll_sha256",
        "certification_report_sha256",
        "ruleset_sha256", "simulator_sha256"
    };
    for (const auto& path : hash_paths) {
        std::string val = get_string(path);
        if (!is_valid_hex64(val)) {
            error_msg = "invalid hash: " + path;
            return false;
        }
    }
    
    // Validate compiled constants match.
    std::int64_t abi = get_int("abi_version");
    if (abi != 1) {
        error_msg = "abi_version mismatch";
        return false;
    }
    std::int64_t ruleset = get_int("ruleset_version");
    if (ruleset != 4) {
        error_msg = "ruleset_version mismatch";
        return false;
    }
    std::int64_t agent_contract = get_int("agent_contract_version");
    if (agent_contract != 2) {
        error_msg = "agent_contract_version mismatch";
        return false;
    }
    std::int64_t obs_size = get_int("observation_size");
    if (obs_size != 64) {
        error_msg = "observation_size mismatch";
        return false;
    }
    std::int64_t hidden_width = get_int("hidden_width");
    if (hidden_width != 512) {
        error_msg = "hidden_width mismatch";
        return false;
    }
    std::int64_t move_count = get_int("move_count");
    if (move_count != 9) {
        error_msg = "move_count mismatch";
        return false;
    }
    std::int64_t aim_count = get_int("aim_count");
    if (aim_count != 9) {
        error_msg = "aim_count mismatch";
        return false;
    }
    std::int64_t button_count = get_int("button_count");
    if (button_count != 4) {
        error_msg = "button_count mismatch";
        return false;
    }
    std::int64_t param_count = get_int("param_count");
    if (param_count != 833047) {
        error_msg = "param_count mismatch";
        return false;
    }
    std::int64_t obs_schema = get_int("observation_schema_version");
    if (obs_schema != 1) {
        error_msg = "observation_schema_version mismatch";
        return false;
    }
    std::int64_t action_schema = get_int("action_schema_version");
    if (action_schema != 1) {
        error_msg = "action_schema_version mismatch";
        return false;
    }
    std::int64_t tensor_count = get_int("tensor_count");
    if (tensor_count != 16) {
        error_msg = "tensor_count mismatch";
        return false;
    }
    
    // Validate golden case count >= 256 (nested: golden.case_count)
    std::int64_t golden_count = get_int("golden.case_count");
    if (golden_count < 256) {
        error_msg = "golden case count must be >= 256";
        return false;
    }
    
    // Validate checkpoint update and bytes are present.
    std::int64_t checkpoint_update = get_int("checkpoint_update");
    if (checkpoint_update < 0) {
        error_msg = "invalid checkpoint_update";
        return false;
    }
    std::int64_t checkpoint_bytes = get_int("checkpoint_bytes");
    if (checkpoint_bytes <= 0) {
        error_msg = "invalid checkpoint_bytes";
        return false;
    }
    
    // Validate checkpoint branch name
    std::string checkpoint_branch = get_string("checkpoint_branch_name");
    if (checkpoint_branch.empty()) {
        error_msg = "missing checkpoint_branch_name";
        return false;
    }
    
    // Validate bundle/golden filenames match expected (nested structure).
    std::string bundle_filename = get_string("bundle.filename");
    if (bundle_filename != "pulse_zero_policy.pzb") {
        error_msg = "bundle filename mismatch";
        return false;
    }
    std::string golden_filename = get_string("golden.filename");
    if (golden_filename != "pulse_zero_policy.golden.pzg") {
        error_msg = "golden filename mismatch";
        return false;
    }
    
    // Validate magic strings (nested structure).
    std::string bundle_magic = get_string("bundle.magic");
    if (bundle_magic != "PZPB") {
        error_msg = "bundle magic mismatch";
        return false;
    }
    std::string golden_magic = get_string("golden.magic");
    if (golden_magic != "PZGF") {
        error_msg = "golden magic mismatch";
        return false;
    }
    
    // Validate bundle/golden versions (nested structure).
    std::int64_t bundle_version = get_int("bundle.version");
    if (bundle_version != 1) {
        error_msg = "bundle version mismatch";
        return false;
    }
    std::int64_t golden_version = get_int("golden.version");
    if (golden_version != 1) {
        error_msg = "golden version mismatch";
        return false;
    }
    
    // Validate endian (nested structure).
    std::string bundle_endian = get_string("bundle.endian");
    if (bundle_endian != "little") {
        error_msg = "endian mismatch";
        return false;
    }
    
    // Validate reward contract (nested object with identity and zero bonuses).
    const JsonNode* reward_contract = get_field("reward_contract", JsonType::Object);
    if (!reward_contract) {
        error_msg = "missing or invalid reward_contract";
        return false;
    }
    
    // Navigate within reward_contract to check identity and bonuses
    const JsonNode* is_identity = nullptr;
    for (const auto& kv : reward_contract->object_val) {
        if (kv.first == "is_identity") {
            is_identity = &kv.second;
            break;
        }
    }
    if (!is_identity || is_identity->type != JsonType::Bool || !is_identity->bool_val) {
        error_msg = "reward_contract is_identity must be true";
        return false;
    }
    
    const JsonNode* bonus_last_touch = nullptr;
    for (const auto& kv : reward_contract->object_val) {
        if (kv.first == "bonus_last_touch") {
            bonus_last_touch = &kv.second;
            break;
        }
    }
    if (!bonus_last_touch || bonus_last_touch->type != JsonType::Number) {
        error_msg = "reward_contract missing bonus_last_touch";
        return false;
    }
    if (std::abs(bonus_last_touch->float_val) > 1e-6) {
        error_msg = "reward_contract bonus_last_touch must be zero";
        return false;
    }
    
    const JsonNode* bonus_controlled = nullptr;
    for (const auto& kv : reward_contract->object_val) {
        if (kv.first == "bonus_controlled") {
            bonus_controlled = &kv.second;
            break;
        }
    }
    if (!bonus_controlled || bonus_controlled->type != JsonType::Number) {
        error_msg = "reward_contract missing bonus_controlled";
        return false;
    }
    if (std::abs(bonus_controlled->float_val) > 1e-6) {
        error_msg = "reward_contract bonus_controlled must be zero";
        return false;
    }
    
    // Validate tensor schema (array of objects with name and shape).
    const JsonNode* tensor_schema = get_field("tensor_schema", JsonType::Array);
    if (!tensor_schema) {
        error_msg = "missing or invalid tensor_schema";
        return false;
    }
    if (tensor_schema->array_val.size() != 16) {
        error_msg = "tensor_schema must have 16 entries";
        return false;
    }
    
    // Validate created_utc is present and non-empty.
    std::string created_utc = get_string("created_utc");
    if (created_utc.empty()) {
        error_msg = "missing created_utc";
        return false;
    }
    
    // Validate versions object (nested structure).
    const JsonNode* versions = get_field("versions", JsonType::Object);
    if (!versions) {
        error_msg = "missing or invalid versions";
        return false;
    }
    
    return true;
}

bool Policy::populate_provenance(const std::string& manifest_json,
                                const std::string& bundle_sha,
                                std::string& error_msg) noexcept {
    if (!parsed_manifest_ || parsed_manifest_->type != JsonType::Object) {
        error_msg = "Parsed manifest is not an object";
        return false;
    }
    
    const JsonNode& root = static_cast<const JsonNode&>(*parsed_manifest_);
    
    // Helper to navigate to a path and get string value
    auto get_string = [&](const std::string& path) -> std::string {
        std::string err;
        const JsonNode* node = navigate_json(root, path, err);
        if (!node || node->type != JsonType::String) return "";
        return node->string_val;
    };
    
    // Populate required provenance fields
    provenance_.bundle_sha256 = bundle_sha;
    provenance_.manifest_sha256 = sha256_hex(
        reinterpret_cast<const unsigned char*>(manifest_json.c_str()),
        manifest_json.size()
    );
    provenance_.checkpoint_sha256 = get_string("checkpoint_sha256");
    provenance_.certification_sha256 = get_string("certification_report_sha256");
    provenance_.dll_sha256 = get_string("dll_sha256");
    provenance_.config_sha256 = get_string("config_hash");
    provenance_.ruleset_sha256 = get_string("ruleset_sha256");
    provenance_.simulator_sha256 = get_string("simulator_sha256");
    
    // Validate all required fields are populated and valid
    if (!provenance_.is_populated()) {
        error_msg = "missing required provenance fields";
        provenance_.clear();
        return false;
    }
    
    // Validate SHA-256 format for all fields — no optional empty fallback.
    auto validate_sha = [&](const std::string& sha, const std::string& name) -> bool {
        if (sha.empty()) {
            error_msg = "missing required SHA-256 for " + name;
            return false;
        }
        if (!is_valid_hex64(sha)) {
            error_msg = "invalid SHA-256 format for " + name;
            return false;
        }
        return true;
    };
    
    if (!validate_sha(provenance_.bundle_sha256, "bundle_sha256")) {
        provenance_.clear();
        return false;
    }
    if (!validate_sha(provenance_.manifest_sha256, "manifest_sha256")) {
        provenance_.clear();
        return false;
    }
    if (!validate_sha(provenance_.checkpoint_sha256, "checkpoint_sha256")) {
        provenance_.clear();
        return false;
    }
    if (!validate_sha(provenance_.certification_sha256, "certification_sha256")) {
        provenance_.clear();
        return false;
    }
    if (!validate_sha(provenance_.dll_sha256, "dll_sha256")) {
        provenance_.clear();
        return false;
    }
    if (!validate_sha(provenance_.config_sha256, "config_sha256")) {
        provenance_.clear();
        return false;
    }
    if (!validate_sha(provenance_.ruleset_sha256, "ruleset_sha256")) {
        provenance_.clear();
        return false;
    }
    if (!validate_sha(provenance_.simulator_sha256, "simulator_sha256")) {
        provenance_.clear();
        return false;
    }
    
    return true;
}

bool Policy::is_valid_hex64(const std::string& s) noexcept {
    if (s.size() != 64) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

}  // namespace pulse
