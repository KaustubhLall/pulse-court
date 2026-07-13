#pragma once

#include "pulse_sim.hpp"

#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <gdiplus.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace pulse {

// Color palette shared between the viewer and the presentation helper.
inline constexpr COLORREF kCourtBackground = RGB(15, 35, 25);
inline constexpr COLORREF kCourtLines = RGB(70, 100, 70);
inline constexpr COLORREF kGoalArea = RGB(35, 20, 20);
inline constexpr COLORREF kWallColor = RGB(220, 220, 220);
inline constexpr COLORREF kPlayer1Color = RGB(60, 100, 220);
inline constexpr COLORREF kPlayer2Color = RGB(220, 80, 60);
inline constexpr COLORREF kCoreColor = RGB(240, 240, 240);
inline constexpr COLORREF kPanelBackground = RGB(25, 30, 40);
inline constexpr COLORREF kPanelBorder = RGB(70, 80, 100);
inline constexpr COLORREF kTextMain = RGB(220, 220, 220);
inline constexpr COLORREF kTextDim = RGB(150, 150, 150);
inline constexpr COLORREF kCooldownBar = RGB(100, 150, 200);
inline constexpr COLORREF kCooldownEmpty = RGB(50, 60, 70);
inline constexpr COLORREF kPlayer1CardFill = RGB(25, 45, 80);
inline constexpr COLORREF kPlayer2CardFill = RGB(80, 40, 35);
inline constexpr COLORREF kPolicyFill = RGB(25, 30, 40);

// GDI helpers
void draw_circle(HDC hdc, int x, int y, int r, COLORREF fill, COLORREF stroke);
void draw_hollow_circle(HDC hdc, int x, int y, int r, COLORREF stroke,
                        int style = PS_SOLID, int width = 2);
void draw_line(HDC hdc, int x1, int y1, int x2, int y2, COLORREF color,
               int width = 2);
void draw_rect(HDC hdc, RECT rc, COLORREF fill, COLORREF stroke = RGB(0, 0, 0));
void draw_text(HDC hdc, int x, int y, const char* text, COLORREF color,
               int height = 14, const char* font = "Consolas");
void draw_cooldown_bar(HDC hdc, int x, int y, int width, int height,
                       std::int32_t current, std::int32_t max_val,
                       const char* label);

// Coordinate conversion
int world_to_screen_x(std::int32_t world_x, int court_width);
int world_to_screen_y(std::int32_t world_y, int court_height);
int world_radius_to_screen(std::int32_t radius, int court_width);

// Locate the assets directory next to the executable, falling back to the
// process working directory.
std::filesystem::path find_assets_directory();

enum class SheetId : std::size_t {
    Arena,
    AbilityIcons,
    CoreSpin,
    CoreImpact,
    StrikeSweep,
    DashTrail,
    JetstepFx,
    AnchorWellFx,
    PulseGateFx,
    GoalBurst,
    KickoffPulse,
    KiteIdle,
    KiteMoveLoop,
    KiteMoveTransition,
    KiteStrike,
    KiteDash,
    KiteAbility,
    KiteGoalWin,
    KiteGoalLoss,
    ValeIdle,
    ValeMoveLoop,
    ValeMoveTransition,
    ValeStrike,
    ValeDash,
    ValeAbility,
    ValeGoalWin,
    ValeGoalLoss,
    BastionIdle,
    BastionMoveLoop,
    BastionMoveTransition,
    BastionStrike,
    BastionDash,
    BastionAbility,
    BastionGoalWin,
    BastionGoalLoss,
    Count
};

class AssetManager {
public:
    struct SheetInfo {
        const wchar_t* filename;
        int columns;
        int rows;
        int frames;
    };

    bool load(const std::filesystem::path& assets_dir);
    [[nodiscard]] Gdiplus::Bitmap* get(SheetId id) const;
    [[nodiscard]] bool has(SheetId id) const;
    [[nodiscard]] static const SheetInfo& info(SheetId id);
    [[nodiscard]] const std::filesystem::path& assets_dir() const;
    [[nodiscard]] const std::string& fallback_status() const;

    // Draw a single frame. The source rectangle is derived from the sheet grid.
    // angle_deg rotates around (center_x, center_y). alpha 0..1 fades the sprite.
    [[nodiscard]] bool draw_sprite(HDC hdc, SheetId id, int frame, int center_x,
                                   int center_y, int dest_w, int dest_h,
                                   float angle_deg = 0.0f,
                                   float alpha = 1.0f) const;

    // Draw a full image cropped to cover a destination rectangle while preserving
    // the source aspect ratio.
    [[nodiscard]] bool draw_image_cropped(HDC hdc, SheetId id, int x, int y,
                                          int w, int h) const;

private:
    std::filesystem::path assets_dir_;
    std::array<std::unique_ptr<Gdiplus::Bitmap>,
               static_cast<std::size_t>(SheetId::Count)>
        bitmaps_;
    static const SheetInfo kSheets[static_cast<std::size_t>(SheetId::Count)];
    std::string fallback_status_;
};

class AnimationController {
public:
    void reset(const std::array<Character, 2>& chars = {Character::Kite,
                                                        Character::Vale});
    void update(const GameState& state, const StepEvents& events);

    void draw_entities(HDC hdc, const AssetManager& assets, int court_x,
                       int court_y, int court_width,
                       int court_height) const;

    struct BodyDebug {
        const char* anim_name = "";
        int elapsed = 0;
        SheetId sheet = SheetId::KiteIdle;
        int frame = 0;
        int frame_count = 0;
    };
    BodyDebug body_debug(std::size_t idx) const;

private:
    enum class BodyAnim : std::uint8_t {
        Idle,
        MoveStart,
        Move,
        MoveStop,
        Strike,
        Dash,
        Ability,
        GoalWin,
        GoalLoss
    };

    struct BodyState {
        std::int8_t player_idx = -1;
        BodyAnim anim = BodyAnim::Idle;
        std::int32_t elapsed = 0;
        Vec2 facing{};
        bool moving = false;
        bool pre_action_moving = false;
        bool strike_hit = false;
    };

    struct PersistentEffect {
        bool active = false;
        EffectKind kind = EffectKind::None;
        Vec2 position{};
        Vec2 direction{};
        std::int32_t elapsed = 0;
        std::int32_t active_duration = 0;
        std::int32_t display_duration = 0;
        bool follow_player = false;
    };

    struct Vfx {
        SheetId sheet;
        std::int32_t frame_duration = 1;
        std::int32_t lifetime = 0;
        std::int32_t elapsed = 0;
        Vec2 position{};
        Vec2 direction{};
        std::int8_t actor = -1;
    };

    std::array<BodyState, 2> bodies_;
    std::array<PersistentEffect, 2> persistent_;
    std::vector<Vfx> trails_;
    std::vector<Vfx> overlays_;
    GameState state_{};
    std::array<Character, 2> chars_ = {Character::Kite, Character::Vale};

    void resolve_action_end(BodyState& b);
    void add_vfx(std::vector<Vfx>& list, SheetId sheet, std::int32_t lifetime,
                 std::int32_t frame_duration, Vec2 position, Vec2 direction,
                 std::int8_t actor = -1);
    [[nodiscard]] SheetId body_sheet(Character character, BodyAnim anim) const;

    struct BodyFrame {
        SheetId sheet = SheetId::KiteIdle;
        int frame = 0;
    };
    [[nodiscard]] BodyFrame resolve_body_frame(Character character,
                                               const BodyState& b,
                                               int row) const;
    [[nodiscard]] static const char* body_anim_name(BodyAnim anim);
    void draw_body(HDC hdc, const AssetManager& assets, int court_x,
                   int court_y, int court_width, int court_height,
                   std::size_t player_idx) const;
    void draw_persistent(HDC hdc, const AssetManager& assets, int court_x,
                         int court_y, int court_width, int court_height,
                         std::size_t player_idx) const;
    void draw_persistent_fallback(HDC hdc, const PersistentEffect& pe,
                                  int court_x, int court_y, int court_width,
                                  int court_height, std::size_t player_idx) const;
    void draw_vfx(HDC hdc, const AssetManager& assets, int court_x, int court_y,
                  int court_width, int court_height,
                  const Vfx& vfx) const;
};

// Draw the procedural court lines, goals, and walls over the arena.
// When arena art is available, opaque goal fills are omitted so the arena
// goal art remains visible.
void draw_court_lines(HDC hdc, const AssetManager& assets, int court_x,
                      int court_y, int court_width, int court_height);

}  // namespace pulse
