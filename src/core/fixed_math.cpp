#include "fixed_math.hpp"

namespace pulse {

std::uint64_t isqrt64(std::uint64_t x) noexcept {
    if (x <= 1) return x;
    std::uint64_t r = x;
    std::uint64_t tmp;
    do {
        tmp = r;
        r = (r + x / r) >> 1;
    } while (r < tmp);
    return tmp;
}

Vec2 normalize_dir(std::int32_t dx, std::int32_t dy) noexcept {
    std::int64_t len_sq = static_cast<std::int64_t>(dx) * dx +
                          static_cast<std::int64_t>(dy) * dy;
    if (len_sq == 0) return {0, 0};
    // scaled_len = floor(sqrt(len_sq) * kFixedScale)
    std::uint64_t scaled_len =
        isqrt64(len_sq * static_cast<std::uint64_t>(kFixedScale) *
                static_cast<std::uint64_t>(kFixedScale));
    if (scaled_len == 0) return {0, 0};
    std::int64_t nx = static_cast<std::int64_t>(dx) * kFixedScale *
                      kFixedScale / static_cast<std::int64_t>(scaled_len);
    std::int64_t ny = static_cast<std::int64_t>(dy) * kFixedScale *
                      kFixedScale / static_cast<std::int64_t>(scaled_len);
    return {static_cast<std::int32_t>(nx), static_cast<std::int32_t>(ny)};
}

std::int64_t length_sq(Vec2 v) noexcept {
    return static_cast<std::int64_t>(v.x) * v.x +
           static_cast<std::int64_t>(v.y) * v.y;
}

Vec2 cap_length(Vec2 v, std::int32_t max_len) noexcept {
    if (max_len <= 0) return {0, 0};
    std::int64_t len = isqrt64(static_cast<std::uint64_t>(length_sq(v)));
    if (len <= max_len) return v;
    std::int64_t nx =
        static_cast<std::int64_t>(v.x) * max_len / len;
    std::int64_t ny =
        static_cast<std::int64_t>(v.y) * max_len / len;
    return {static_cast<std::int32_t>(nx), static_cast<std::int32_t>(ny)};
}

Vec2 reflect(Vec2 v, Vec2 n) noexcept {
    // dot = n · v  (n is unit * scale, so dot is scale * (real dot))
    std::int64_t dot = static_cast<std::int64_t>(n.x) * v.x +
                       static_cast<std::int64_t>(n.y) * v.y;
    // real reflection: v' = v - 2 * (n/scale) * (dot/scale)
    //                 = v - (2 * n * dot) / scale^2
    std::int64_t scale2 = static_cast<std::int64_t>(kFixedScale) * kFixedScale;
    std::int64_t rx =
        v.x - (static_cast<std::int64_t>(2) * n.x * dot) / scale2;
    std::int64_t ry =
        v.y - (static_cast<std::int64_t>(2) * n.y * dot) / scale2;
    return {static_cast<std::int32_t>(rx), static_cast<std::int32_t>(ry)};
}

Vec2 push(Vec2 start, Vec2 dir, std::int64_t num,
          std::int64_t den) noexcept {
    if (den == 0) return start;
    std::int64_t x = static_cast<std::int64_t>(start.x) +
                     static_cast<std::int64_t>(dir.x) * num / den;
    std::int64_t y = static_cast<std::int64_t>(start.y) +
                     static_cast<std::int64_t>(dir.y) * num / den;
    return {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
}

}  // namespace pulse
