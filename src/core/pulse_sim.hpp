#pragma once

#include <array>
#include <cstdint>

namespace pulse {

// All authoritative quantities are represented with this binary fixed-point
// scale. 1024 units == 1.0 world units. The core simulation deliberately
// contains no floating-point operations in physics, state, collision, replay,
// or hashing.
inline constexpr std::int32_t kFixedScale = 1024;
inline constexpr std::int32_t kSimHz = 120;

// Court dimensions in fixed units.
inline constexpr std::int32_t kFieldWidth = 32 * kFixedScale;   // 32768
inline constexpr std::int32_t kFieldHeight = 18 * kFixedScale;  // 18432
inline constexpr std::int32_t kFieldHalfX = kFieldWidth / 2;    // 16384
inline constexpr std::int32_t kFieldHalfY = kFieldHeight / 2;   // 9216
inline constexpr std::int32_t kGoalTop = 6 * kFixedScale;       // 6144
inline constexpr std::int32_t kGoalBottom = 12 * kFixedScale;   // 12288

// Entity radii (fixed units).
inline constexpr std::int32_t kPlayerRadius = 737;  // ~0.72 world units
inline constexpr std::int32_t kCoreRadius = 358;    // ~0.35 world units

// Simulation substeps for the core per 120 Hz tick.
inline constexpr std::int32_t kCoreSubsteps = 2;

// Movement and physics constants (fixed units per tick where applicable).
// These are deliberately modest for a 32-unit court: the core can outpace a
// player, but cannot traverse the field in a few simulation frames.
inline constexpr std::int32_t kPlayerMaxSpeed = 80;     // ~9.4 world units/sec
inline constexpr std::int32_t kPlayerAccel = 4;         // fixed units/tick^2
inline constexpr std::int32_t kPlayerFriction = 4;      // fixed units/tick^2
inline constexpr std::int32_t kCoreMaxSpeed = 300;      // ~35.2 world units/sec
inline constexpr std::int32_t kCoreDrag = 2;            // fixed units/tick^2
inline constexpr std::int32_t kCoreRestitution = 896;   // 87.5%, scaled by 1024
inline constexpr std::int32_t kDashSpeed = 150;         // ~17.6 world units/sec

inline constexpr std::int32_t kStrikeReach = 768;       // 0.75 world unit reach
inline constexpr std::int32_t kStrikeImpulse = 170;     // fixed units/tick
inline constexpr std::int32_t kStrikeMinForwardSpeed = 120;
inline constexpr std::int32_t kStrikeForwardCos = 256;  // cos(theta) * 1024
inline constexpr std::int32_t kStrikeSeparation = 48;
inline constexpr std::int32_t kStrikeDuration = 6;      // ticks
inline constexpr std::int32_t kStrikeCooldown = 36;     // ticks
inline constexpr std::int32_t kDashCooldown = 144;      // ticks

// Character-specific tuning.
inline constexpr std::int32_t kKiteStrikeBonus = 90;            // fixed/tick
inline constexpr std::int32_t kJetstepSpeedBonus = 60;          // fixed/tick
inline constexpr std::int32_t kJetstepDuration = 45;            // ticks
inline constexpr std::int32_t kAbilityCooldown = 300;           // ticks
inline constexpr std::int32_t kAnchorOffset = 4 * kFixedScale;  // 4.0 units
inline constexpr std::int32_t kAnchorRadius = 7 * kFixedScale;  // 7.0 units
inline constexpr std::int32_t kAnchorStrength = 8;              // fixed/tick^2
inline constexpr std::int32_t kAnchorDuration = 120;            // ticks
inline constexpr std::int32_t kGateOffset = 4 * kFixedScale;    // 4.0 units
inline constexpr std::int32_t kGateRadius = (5 * kFixedScale) / 2;  // 2.5 units
inline constexpr std::int32_t kGateDuration = 120;              // ticks

inline constexpr std::uint32_t kRulesetVersion = 1;

struct Vec2 {
    std::int32_t x = 0;
    std::int32_t y = 0;

    [[nodiscard]] constexpr bool operator==(const Vec2&) const = default;
};

enum class Character : std::uint8_t {
    Kite = 0,
    Vale = 1,
    Bastion = 2,
};

enum class Phase : std::uint8_t {
    Kickoff = 0,
    Live = 1,
    MatchOver = 2,
};

enum class EffectKind : std::uint8_t {
    None = 0,
    Jetstep = 1,
    AnchorWell = 2,
    PulseGate = 3,
};

enum InputButton : std::uint8_t {
    ButtonNone = 0,
    ButtonStrike = 1 << 0,
    ButtonAbility = 1 << 1,
    ButtonDash = 1 << 2,
};

struct FrameInput {
    std::int8_t move_x = 0;
    std::int8_t move_y = 0;
    std::int8_t aim_x = 0;
    std::int8_t aim_y = 0;
    std::uint8_t buttons = ButtonNone;

    [[nodiscard]] constexpr bool operator==(const FrameInput&) const = default;
};

struct CoreState {
    Vec2 position{};
    Vec2 velocity{};

    [[nodiscard]] constexpr bool operator==(const CoreState&) const = default;
};

struct PlayerState {
    Character character = Character::Kite;
    Vec2 position{};
    Vec2 velocity{};
    Vec2 facing{};
    Vec2 effect_position{};
    EffectKind effect_kind = EffectKind::None;
    bool jetstep_bonus_used = false;
    bool strike_has_hit = false;
    std::int32_t strike_ticks = 0;
    std::int32_t strike_cooldown = 0;
    std::int32_t dash_cooldown = 0;
    std::int32_t ability_cooldown = 0;
    std::int32_t effect_ticks = 0;

    [[nodiscard]] constexpr bool operator==(const PlayerState&) const = default;
};

struct MatchConfig {
    std::int32_t score_to_win = 5;
    std::int32_t regulation_ticks = 180 * kSimHz;  // 21600
    std::int32_t kickoff_ticks = 90;

    [[nodiscard]] constexpr bool operator==(const MatchConfig&) const = default;
};

struct GameState {
    std::uint32_t ruleset_version = kRulesetVersion;
    std::uint32_t tick = 0;
    Phase phase = Phase::Kickoff;
    std::int32_t kickoff_remaining = 0;
    std::int32_t regulation_remaining = 0;
    bool golden_goal = false;
    std::array<std::int32_t, 2> score{};
    std::array<PlayerState, 2> players{};
    CoreState core{};
    std::int8_t last_touch = -1;
    MatchConfig config{};

    [[nodiscard]] bool operator==(const GameState&) const = default;
};

struct StepResult {
    bool goal_scored = false;
    std::int8_t scoring_team = -1;
    bool match_finished = false;
};

class Simulation {
public:
    static constexpr std::int32_t kActionSpaceSize = 9 * 9 * 4;

    explicit Simulation(
        std::array<Character, 2> characters = {Character::Kite, Character::Vale},
        MatchConfig config = {});

    void reset(std::array<Character, 2> characters);
    void set_state(const GameState& restored_state);

    [[nodiscard]] const GameState& state() const noexcept;
    [[nodiscard]] const MatchConfig& config() const noexcept;
    [[nodiscard]] StepResult step(const std::array<FrameInput, 2>& inputs);
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

    [[nodiscard]] static FrameInput decode_action(std::int32_t action_index) noexcept;
    [[nodiscard]] static FrameInput mirror_input(const FrameInput& input) noexcept;
    [[nodiscard]] static GameState mirror_state(const GameState& state) noexcept;

private:
    GameState state_{};
    MatchConfig config_{};
    std::array<FrameInput, 2> last_inputs_{};
    std::array<bool, 2> dash_fired_{};

    void reset_positions();
    void decrement_timers();
    void apply_player_input(std::int32_t player_index, FrameInput input);
    void resolve_player_motion();
    void resolve_strikes();
    void apply_effect_forces();
    void apply_core_drag();
    void advance_core(StepResult& result);
    bool check_goal_line(StepResult& result);
    void score_goal(std::int32_t team, StepResult& result);
    void update_clock(StepResult& result);
};

[[nodiscard]] const char* character_name(Character character) noexcept;
[[nodiscard]] bool parse_character(const char* text, Character& character) noexcept;

}  // namespace pulse
