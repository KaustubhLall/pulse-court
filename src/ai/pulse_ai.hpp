#pragma once

#include "pulse_sim.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pulse::ai {

// Agent contract version. Observed results must identify the contract that
// produced them; bump this only via a new locked contract document.
inline constexpr std::uint32_t kAgentContractVersion = 2;

// Observation v2 size, locked in docs/pulse-zero-agent-contract-v2.md.
inline constexpr std::size_t kObservationSize = 64;
using Observation = std::array<float, kObservationSize>;

// Idle-safety horizon for scenario v2: both players idle for this many ticks
// must produce no goal. 2 seconds at the 120 Hz simulation rate.
inline constexpr std::int32_t kScenarioIdleSafetyTicks = 2 * kSimHz;

// Deterministic scripted ladder rungs.
enum class BotKind : std::uint8_t {
    Idle = 0,
    Chaser = 1,
    Goalie = 2,
    Tactical = 3,
};

[[nodiscard]] const char* bot_kind_name(BotKind kind) noexcept;
[[nodiscard]] bool parse_bot_kind(const char* text, BotKind& kind) noexcept;

// Factor legality mask. Values are exactly 0 or 1. Movement and aim are always
// fully legal; button none is always legal; strike/ability/dash are legal only
// when their cooldown (and strike_ticks for strike) is zero.
struct ActionMask {
    std::array<std::uint8_t, 9> move{};
    std::array<std::uint8_t, 9> aim{};
    std::array<std::uint8_t, 4> button{};
};

// Encode the perspective-canonical observation for `player_index`. Player 1 is
// mirrored via Simulation::mirror_state so the observed player is always
// canonical player 0 attacking right. Out-of-range indices return a zero
// observation. This is non-authoritative model input only.
//
// NOTE (main-only, non-parity): indices 16, 18, 19 (touch-kind Strike /
// AnchorWell / PulseGate one-hot slots) can never be 1.0 on main's core --
// see src/ai/pulse_core_compat.hpp for why. The 64-float layout itself is
// unchanged from the ruleset-4 contract.
[[nodiscard]] Observation encode_observation(const GameState& state,
                                             std::size_t player_index) noexcept;

// Build the factor legality mask for `player_index` (canonicalized the same
// way as the observation). Out-of-range indices return a zero mask.
[[nodiscard]] ActionMask make_action_mask(const GameState& state,
                                          std::size_t player_index) noexcept;

// Pure scripted action choice for canonical player 0 attacking right. For
// physical player 1 the state is mirrored, the choice made as player 0, and
// the input mirrored back. Emits at most one button bit with priority
// strike, ability, dash and never a cooldown-invalid button. Out-of-range
// indices return a zero input.
[[nodiscard]] FrameInput choose_scripted_action(BotKind kind,
                                                const GameState& state,
                                                std::size_t player_index) noexcept;

// A controller output: the held/pulsed input plus a flag marking a real fresh
// decision-cadence choice (stable for an idempotent same-tick repeat).
struct ControllerOutput {
    FrameInput input{};
    bool decision = false;
};

// Decision-cadence controller wrapping a scripted bot. Chooses immediately on
// first use, reset/tick rewind, phase transition, or player-index change;
// otherwise on state ticks divisible by kDecisionIntervalTicks and only once
// per tick. Holds movement and aim between choices; pulses the chosen button
// only on the choice tick. Returns zero input in MatchOver. Idempotent on an
// unchanged state tick for the same player index.
class BotController {
public:
    explicit BotController(BotKind kind) noexcept : kind_(kind) {}

    void reset() noexcept;
    [[nodiscard]] BotKind kind() const noexcept { return kind_; }
    [[nodiscard]] ControllerOutput act(const GameState& state,
                                       std::size_t player_index) noexcept;

private:
    BotKind kind_;
    bool initialized_ = false;
    std::uint32_t last_seen_tick_ = 0;
    Phase last_phase_ = Phase::MatchOver;
    std::size_t last_player_index_ = 0;
    std::uint32_t last_decision_tick_ = 0;
    FrameInput held_input_{};
    // Cached output for the current (tick, phase, player) key, for same-key
    // idempotency.
    bool has_cached_ = false;
    std::uint32_t cached_tick_ = 0;
    Phase cached_phase_ = Phase::MatchOver;
    std::size_t cached_player_index_ = 0;
    FrameInput cached_input_{};
    bool cached_decision_ = false;
};

// ---- scenario generators ---------------------------------------------------

// Scenario v1 generator. AUDIT-ONLY / DEPRECATED: preserved for reproducibility
// at its committed checkpoint. Current arena selection uses v2. Do not use v1
// starts for checkpoint selection, teacher ranking, or training.
[[nodiscard]] GameState make_scenario_v1(std::uint64_t seed,
                                         std::array<Character, 2> characters,
                                         std::int32_t max_ticks) noexcept;

// Validate all scenario v1 invariants. AUDIT-ONLY / DEPRECATED.
[[nodiscard]] bool scenario_v1_is_valid(const GameState& state) noexcept;

// Scenario v2 generator. Uses a private local SplitMix64 derived only from
// `seed`. Creates a live, scoreless, score-to-one state with no active effects,
// player 0 in the left half and player 1 in the right half, no overlaps, core
// x in the middle third, core speed strictly below kStrikeMinForwardSpeed,
// each player/core center distance at least 3*kFixedScale, and an idle-safety
// probe (both players idle for kScenarioIdleSafetyTicks) that produces no goal.
// `max_ticks <= 0` uses 60*kSimHz. A bounded 4096-attempt retry loop falls back
// to a documented safe layout.
[[nodiscard]] GameState make_scenario_v2(std::uint64_t seed,
                                         std::array<Character, 2> characters,
                                         std::int32_t max_ticks) noexcept;

// Validate all scenario v2 invariants, including the idle-safety simulation
// probe so a later generator edit cannot silently weaken it.
[[nodiscard]] bool scenario_v2_is_valid(const GameState& state) noexcept;

}  // namespace pulse::ai
