#pragma once

#include "pulse_sim.hpp"

#include <cstdint>

namespace pulse {

// Integer square root. No floating point.
[[nodiscard]] std::uint64_t isqrt64(std::uint64_t x) noexcept;

// Normalize a small integer direction vector to fixed-point unit length.
// (dx,dy) is expected to be one of the 9 cardinal/diagonal values.
[[nodiscard]] Vec2 normalize_dir(std::int32_t dx, std::int32_t dy) noexcept;

// Return a vector's squared length in fixed units squared.
[[nodiscard]] std::int64_t length_sq(Vec2 v) noexcept;

// Reduce |v| to max_len (fixed units) if it is longer.
[[nodiscard]] Vec2 cap_length(Vec2 v, std::int32_t max_len) noexcept;

// Reflect velocity v about a fixed-point unit normal n. n points away from the
// surface that is being collided with. This uses 64-bit intermediates and
// division by kFixedScale^2.
[[nodiscard]] Vec2 reflect(Vec2 v, Vec2 n) noexcept;

// Linear interpolation/push: return start + dir * num / den.
[[nodiscard]] Vec2 push(Vec2 start, Vec2 dir, std::int64_t num,
                        std::int64_t den) noexcept;

}  // namespace pulse
