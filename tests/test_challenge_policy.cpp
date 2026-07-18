#include "pulse_policy.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <sstream>

namespace pulse {
namespace tests {

static int g_passed = 0;
static int g_failed = 0;

void check(bool cond, const char* name) {
    if (cond) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "FAIL: " << name << "\n";
    }
}

void report() {
    std::cout << "Tests: " << g_passed << " passed, " << g_failed
              << " failed\n";
    if (g_failed > 0) std::exit(1);
}

namespace {

void write_u32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

void write_u64_le(std::uint8_t* p, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(v >> (i * 8));
}

std::string create_synthetic_bundle(const std::filesystem::path& dir,
                                     std::uint32_t weights_count) {
    std::filesystem::create_directories(dir);
    std::vector<float> weights(weights_count);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& w : weights) w = dist(rng);

    std::vector<std::uint8_t> bytes(20 + weights_count * 4);
    bytes[0] = 'P';
    bytes[1] = 'Z';
    bytes[2] = 'B';
    bytes[3] = 0;
    write_u32_le(&bytes[4], 1);
    write_u32_le(&bytes[16], weights_count);

    std::uint64_t checksum = 0;
    for (std::uint32_t i = 0; i < weights_count; ++i) {
        std::uint32_t u = 0;
        std::memcpy(&u, &weights[i], sizeof(u));
        write_u32_le(&bytes[20 + i * 4], u);
        for (int j = 0; j < 4; ++j) checksum += bytes[20 + i * 4 + j];
    }
    write_u64_le(&bytes[8], checksum);

    std::string bundle_data(bytes.begin(), bytes.end());
    
    // We'll compute SHA256 after creating the policy
    std::ofstream bundle(dir / "synthetic.pzb", std::ios::binary);
    bundle.write(bundle_data.data(),
                 static_cast<std::streamsize>(bundle_data.size()));

    std::ostringstream mss;
    mss << "{\n"
        << "  \"checkpoint_id\": \"synthetic-checkpoint\",\n"
        << "  \"model_version\": \"1.0.0\",\n"
        << "  \"weights_sha256\": \"\",\n"  // Will be filled after load
        << "  \"certification_sha\": \"cert-test\"\n"
        << "}\n";
    std::ofstream manifest(dir / "synthetic.json", std::ios::binary);
    manifest << mss.str();

    return "";  // Return empty, will compute after load
}

}  // namespace
}  // namespace tests
}  // namespace pulse

int main() {
    using namespace pulse;
    using namespace pulse::tests;

    std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pulse_challenge_policy_test";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);

    // SHA-256 known vector for the empty string.
    Policy empty_policy;
    check(empty_policy.sha256() ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "empty string sha256");

    std::string bundle_sha = create_synthetic_bundle(tmp, 36);
    
    // Update manifest with computed SHA256
    {
        Policy temp_policy;
        std::string error;
        std::vector<uint8_t> bundle_data;
        std::ifstream bundle_file(tmp / "synthetic.pzb", std::ios::binary);
        std::copy(std::istreambuf_iterator<char>(bundle_file),
                  std::istreambuf_iterator<char>(),
                  std::back_inserter(bundle_data));
        temp_policy.weights = bundle_data;
        bundle_sha = temp_policy.sha256();
        
        std::ostringstream mss;
        mss << "{\n"
            << "  \"checkpoint_id\": \"synthetic-checkpoint\",\n"
            << "  \"model_version\": \"1.0.0\",\n"
            << "  \"weights_sha256\": \"" << bundle_sha << "\",\n"
            << "  \"certification_sha\": \"cert-test\"\n"
            << "}\n";
        std::ofstream manifest(tmp / "synthetic.json", std::ios::binary);
        manifest << mss.str();
    }

    Policy policy;
    std::string error;
    bool loaded = Policy::load(policy, (tmp / "synthetic.pzb").string(), (tmp / "synthetic.json").string(), error);
    check(loaded, "synthetic bundle loads");
    if (loaded) {
        check(policy.weights.size() == 36 * 4, "weights count (bytes)");
        check(policy.checkpoint_id == "synthetic-checkpoint",
              "checkpoint_id");
        check(policy.model_version == "1.0.0", "model_version");
        check(policy.bundle_sha == bundle_sha,
              "bundle sha matches computed");
        check(policy.is_valid(), "policy is_valid");
    }

    // Bad checksum.
    {
        std::ofstream f(tmp / "bad.pzb", std::ios::binary);
        f.write("PZB\x00", 4);
        std::uint32_t one = 1;
        f.write(reinterpret_cast<const char*>(&one), sizeof(one));
        std::uint64_t zero = 0;
        f.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
        std::uint32_t wc = 1;
        f.write(reinterpret_cast<const char*>(&wc), sizeof(wc));
        float nonzero_f = 1.0f;
        f.write(reinterpret_cast<const char*>(&nonzero_f), sizeof(nonzero_f));
    }
    Policy bad_policy;
    std::string bad_error;
    bool bad_loaded = Policy::load(bad_policy, (tmp / "bad.pzb").string(), (tmp / "synthetic.json").string(), bad_error);
    check(!bad_loaded, "bad checksum rejected");
    check(!bad_error.empty(), "error message present");

    report();
    return 0;
}
