#define NOMINMAX

#include "viewer_assets.hpp"

#include "fixed_math.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace pulse {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string narrow(const wchar_t* w) {
    std::string s;
    for (const wchar_t* c = w; *c; ++c) {
        s += static_cast<char>(*c);
    }
    return s;
}

float direction_angle_deg(Vec2 dir) {
    if (dir.x == 0 && dir.y == 0) return 0.0f;
    double rad = std::atan2(-static_cast<double>(dir.y), static_cast<double>(dir.x));
    return static_cast<float>(rad * 180.0 / kPi);
}

int cardinal_row(Vec2 facing) {
    std::int32_t ax = facing.x >= 0 ? facing.x : -facing.x;
    std::int32_t ay = facing.y >= 0 ? facing.y : -facing.y;
    if (ax > ay) {
        return facing.x >= 0 ? 1 : 3;  // east : west
    }
    return facing.y >= 0 ? 0 : 2;  // north : south
}

}  // namespace

// Presentation-only multiplier for the on-screen player body sprite. The body
// is drawn as a destination square of (world_radius * multiplier) pixels.
// Calibrated to the clean-sheet tile size of 456 px: 5.7 = 4.0 * 456 / 320.
constexpr double kPlayerBodySpriteSizeMultiplier = 5.7;

// Code-driven body pose transforms use a normalized "action progress" curve
// that rises from 0 at action start to 1 at action end and maps through a
// half-sine pulse so each pose returns smoothly to the idle state.

// Fraction of the player radius used for action-specific translations/lunges.
constexpr double kStrikeLungeDistance = 0.5;
constexpr double kDashLeanDistance = 0.35;
constexpr double kAbilityLungeDistance = 0.15;
constexpr double kAbilityLiftDistance = 0.25;
constexpr double kGoalWinJumpDistance = 0.2;
constexpr double kGoalLossSlumpDistance = 0.1;

// Action-specific scale pulses/squashes relative to the base sprite size.
constexpr double kStrikeStretchX = 0.25;
constexpr double kStrikeSquashY = 0.1;
constexpr double kDashStretchX = 0.2;
constexpr double kDashSquashY = 0.05;
constexpr double kAbilityPulseScale = 0.1;
constexpr double kGoalWinPulseScale = 0.1;
constexpr double kGoalLossPulseScale = 0.05;

// Vale Anchor Well presentation scale. The sprite is rendered at this fraction
// of the true diameter (2 * base_r) while the dotted radius ring stays at the
// exact kAnchorRadius gameplay footprint.
constexpr double kAnchorWellArtScale = 0.5;

// GDI helpers
void draw_circle(HDC hdc, int x, int y, int r, COLORREF fill, COLORREF stroke) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 2, stroke);
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
    Ellipse(hdc, x - r, y - r, x + r, y + r);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void draw_hollow_circle(HDC hdc, int x, int y, int r, COLORREF stroke, int style,
                        int width) {
    HPEN pen = CreatePen(style, width, stroke);
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
    HBRUSH old_brush =
        static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Ellipse(hdc, x - r, y - r, x + r, y + r);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(pen);
}

void draw_line(HDC hdc, int x1, int y1, int x2, int y2, COLORREF color,
               int width) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
    MoveToEx(hdc, x1, y1, nullptr);
    LineTo(hdc, x2, y2);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

void draw_rect(HDC hdc, RECT rc, COLORREF fill, COLORREF stroke) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, stroke);
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void draw_text(HDC hdc, int x, int y, const char* text, COLORREF color,
               int height, const char* font) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    HFONT hfont = CreateFontA(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              DEFAULT_PITCH | FF_SWISS,
                              font ? font : "Consolas");
    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, hfont));
    TextOutA(hdc, x, y, text, static_cast<int>(std::strlen(text)));
    SelectObject(hdc, old_font);
    DeleteObject(hfont);
}

void draw_cooldown_bar(HDC hdc, int x, int y, int width, int height,
                       std::int32_t current, std::int32_t max_val,
                       const char* label) {
    RECT bg = {x, y, x + width, y + height};
    draw_rect(hdc, bg, kCooldownEmpty, kCooldownEmpty);
    if (max_val > 0) {
        double ratio = 1.0 - static_cast<double>(current) / max_val;
        int fill_width = static_cast<int>(width * ratio);
        if (fill_width > 0) {
            RECT fill = {x, y, x + fill_width, y + height};
            draw_rect(hdc, fill, kCooldownBar, kCooldownBar);
        }
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%s: %d/%d", label, current, max_val);
    draw_text(hdc, x, y + height + 2, buf, kTextDim, 12);
}

int world_to_screen_x(std::int32_t world_x, int court_width) {
    std::int64_t v = static_cast<std::int64_t>(world_x) * court_width / kFieldWidth;
    return static_cast<int>(v);
}

int world_to_screen_y(std::int32_t world_y, int court_height) {
    std::int64_t v = static_cast<std::int64_t>(world_y) * court_height / kFieldHeight;
    return court_height - static_cast<int>(v);
}

int world_radius_to_screen(std::int32_t radius, int court_width) {
    std::int64_t v = static_cast<std::int64_t>(radius) * court_width / kFieldWidth;
    return static_cast<int>(v);
}

std::filesystem::path find_assets_directory() {
    wchar_t buf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0) {
        std::filesystem::path exe(buf);
        std::filesystem::path candidate = exe.parent_path() / "assets";
        if (std::filesystem::exists(candidate) &&
            std::filesystem::is_directory(candidate)) {
            return candidate;
        }
        candidate = std::filesystem::current_path() / "assets";
        if (std::filesystem::exists(candidate) &&
            std::filesystem::is_directory(candidate)) {
            return candidate;
        }
        return exe.parent_path() / "assets";
    }
    return std::filesystem::current_path() / "assets";
}

const AssetManager::SheetInfo AssetManager::kSheets[static_cast<std::size_t>(
    SheetId::Count)] = {
    {L"arena.png", 1, 1, 1},
    {L"sprites/animated/ability_icons_3x1.png", 3, 1, 3},
    {L"sprites/animated/core_spin_4x2.png", 4, 2, 8},
    {L"sprites/animated/core_impact_3x2.png", 3, 2, 6},
    {L"sprites/animated/strike_sweep_3x2.png", 3, 2, 6},
    {L"sprites/animated/dash_trail_3x2.png", 3, 2, 6},
    {L"sprites/animated/jetstep_fx_6x3.png", 6, 3, 18},
    {L"sprites/animated/anchor_well_fx_6x3.png", 6, 3, 18},
    {L"sprites/animated/pulse_gate_fx_6x3.png", 6, 3, 18},
    {L"sprites/animated/goal_burst_4x3.png", 4, 3, 12},
    {L"sprites/animated/kickoff_pulse_4x2.png", 4, 2, 8},
    {L"sprites/clean/kite_idle_4x4.png", 4, 4, 16},
    {L"sprites/clean/kite_move_loop_4x4.png", 4, 4, 16},
    {L"sprites/clean/kite_move_transition_4x4.png", 4, 4, 16},
    {L"sprites/clean/kite_strike_6x4.png", 6, 4, 24},
    {L"sprites/clean/kite_dash_5x4.png", 5, 4, 20},
    {L"sprites/clean/kite_ability_6x4.png", 6, 4, 24},
    {L"sprites/clean/kite_goal_win_4x4.png", 4, 4, 16},
    {L"sprites/clean/kite_goal_loss_4x4.png", 4, 4, 16},
    {L"sprites/clean/vale_idle_4x4.png", 4, 4, 16},
    {L"sprites/clean/vale_move_loop_4x4.png", 4, 4, 16},
    {L"sprites/clean/vale_move_transition_4x4.png", 4, 4, 16},
    {L"sprites/clean/vale_strike_6x4.png", 6, 4, 24},
    {L"sprites/clean/vale_dash_5x4.png", 5, 4, 20},
    {L"sprites/clean/vale_ability_6x4.png", 6, 4, 24},
    {L"sprites/clean/vale_goal_win_4x4.png", 4, 4, 16},
    {L"sprites/clean/vale_goal_loss_4x4.png", 4, 4, 16},
    {L"sprites/clean/bastion_idle_4x4.png", 4, 4, 16},
    {L"sprites/clean/bastion_move_loop_4x4.png", 4, 4, 16},
    {L"sprites/clean/bastion_move_transition_4x4.png", 4, 4, 16},
    {L"sprites/clean/bastion_strike_6x4.png", 6, 4, 24},
    {L"sprites/clean/bastion_dash_5x4.png", 5, 4, 20},
    {L"sprites/clean/bastion_ability_6x4.png", 6, 4, 24},
    {L"sprites/clean/bastion_goal_win_4x4.png", 4, 4, 16},
    {L"sprites/clean/bastion_goal_loss_4x4.png", 4, 4, 16},
};

bool AssetManager::load(const std::filesystem::path& assets_dir) {
    assets_dir_ = assets_dir;
    fallback_status_.clear();
    std::string missing;
    for (std::size_t i = 0; i < static_cast<std::size_t>(SheetId::Count); ++i) {
        std::filesystem::path path = assets_dir_ / kSheets[i].filename;
        if (!std::filesystem::exists(path)) {
            OutputDebugStringW(
                (L"Pulse Court asset missing: " + path.wstring() + L"\n").c_str());
            if (!missing.empty()) missing += ", ";
            missing += narrow(kSheets[i].filename);
            continue;
        }
        auto bmp = std::make_unique<Gdiplus::Bitmap>(path.c_str());
        if (bmp->GetLastStatus() != Gdiplus::Ok) {
            OutputDebugStringW(
                (L"Pulse Court asset invalid: " + path.wstring() + L"\n").c_str());
            if (!missing.empty()) missing += ", ";
            missing += narrow(kSheets[i].filename);
            continue;
        }
        bitmaps_[i] = std::move(bmp);
    }
    if (!missing.empty()) {
        fallback_status_ = "Procedural fallback: " + missing;
        OutputDebugStringW(
            (L"Pulse Court status: " + std::wstring(fallback_status_.begin(),
                                                    fallback_status_.end()) +
             L"\n")
                .c_str());
    }
    return true;
}

Gdiplus::Bitmap* AssetManager::get(SheetId id) const {
    return bitmaps_[static_cast<std::size_t>(id)].get();
}

const std::string& AssetManager::fallback_status() const {
    return fallback_status_;
}

bool AssetManager::has(SheetId id) const {
    return get(id) != nullptr;
}

const AssetManager::SheetInfo& AssetManager::info(SheetId id) {
    return kSheets[static_cast<std::size_t>(id)];
}

const std::filesystem::path& AssetManager::assets_dir() const {
    return assets_dir_;
}

bool AssetManager::draw_sprite(HDC hdc, SheetId id, int frame, int center_x,
                               int center_y, int dest_w, int dest_h,
                               float angle_deg, float alpha) const {
    Gdiplus::Bitmap* bmp = get(id);
    if (!bmp) return false;

    const SheetInfo& s = info(id);
    if (frame < 0) frame = 0;
    if (frame >= s.frames) frame = s.frames - 1;

    int col = frame % s.columns;
    int row = frame / s.columns;
    int img_w = static_cast<int>(bmp->GetWidth());
    int img_h = static_cast<int>(bmp->GetHeight());

    int src_x = (col * img_w) / s.columns;
    int src_y = (row * img_h) / s.rows;
    int src_w = ((col + 1) * img_w) / s.columns - src_x;
    int src_h = ((row + 1) * img_h) / s.rows - src_y;

    Gdiplus::Graphics gfx(hdc);
    gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    gfx.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::ImageAttributes* attr = nullptr;
    Gdiplus::ImageAttributes attr_obj;
    Gdiplus::ColorMatrix cm;
    if (alpha < 0.99f) {
        std::memset(&cm, 0, sizeof(cm));
        cm.m[0][0] = 1.0f;
        cm.m[1][1] = 1.0f;
        cm.m[2][2] = 1.0f;
        cm.m[3][3] = alpha;
        cm.m[4][4] = 1.0f;
        attr_obj.SetColorMatrix(&cm, Gdiplus::ColorMatrixFlagsDefault,
                                Gdiplus::ColorAdjustTypeBitmap);
        attr = &attr_obj;
    }

    Gdiplus::Rect dest(center_x - dest_w / 2, center_y - dest_h / 2, dest_w,
                       dest_h);
    if (angle_deg != 0.0f) {
        gfx.TranslateTransform(static_cast<Gdiplus::REAL>(center_x),
                               static_cast<Gdiplus::REAL>(center_y),
                               Gdiplus::MatrixOrderAppend);
        gfx.RotateTransform(static_cast<Gdiplus::REAL>(angle_deg),
                            Gdiplus::MatrixOrderAppend);
        dest = Gdiplus::Rect(-dest_w / 2, -dest_h / 2, dest_w, dest_h);
    }

    gfx.DrawImage(bmp, dest, src_x, src_y, src_w, src_h, Gdiplus::UnitPixel,
                  attr, nullptr, nullptr);
    gfx.Flush(Gdiplus::FlushIntentionFlush);
    return true;
}

bool AssetManager::draw_image_cropped(HDC hdc, SheetId id, int x, int y, int w,
                                      int h) const {
    Gdiplus::Bitmap* bmp = get(id);
    if (!bmp) return false;

    int img_w = static_cast<int>(bmp->GetWidth());
    int img_h = static_cast<int>(bmp->GetHeight());
    double src_aspect = static_cast<double>(img_w) / img_h;
    double dst_aspect = static_cast<double>(w) / h;

    int src_x, src_y, src_w, src_h;
    if (src_aspect > dst_aspect) {
        src_h = img_h;
        src_w = static_cast<int>(img_h * dst_aspect);
        src_x = (img_w - src_w) / 2;
        src_y = 0;
    } else {
        src_w = img_w;
        src_h = static_cast<int>(img_w / dst_aspect);
        src_x = 0;
        src_y = (img_h - src_h) / 2;
    }

    Gdiplus::Graphics gfx(hdc);
    gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    gfx.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Rect dest(x, y, w, h);
    gfx.DrawImage(bmp, dest, src_x, src_y, src_w, src_h, Gdiplus::UnitPixel);
    gfx.Flush(Gdiplus::FlushIntentionFlush);
    return true;
}

void AnimationController::reset(const std::array<Character, 2>& chars) {
    chars_ = chars;
    state_ = GameState{};
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        bodies_[i].player_idx = static_cast<std::int8_t>(i);
        bodies_[i].anim = BodyAnim::Idle;
        bodies_[i].elapsed = 0;
        bodies_[i].facing = (i == 0) ? Vec2{kFixedScale, 0} : Vec2{-kFixedScale, 0};
        bodies_[i].moving = false;
        bodies_[i].pre_action_moving = false;
        bodies_[i].strike_hit = false;
    }
    for (auto& pe : persistent_) {
        pe = PersistentEffect{};
    }
    trails_.clear();
    overlays_.clear();
}

void AnimationController::resolve_action_end(BodyState& b) {
    if (b.moving && b.pre_action_moving) {
        b.anim = BodyAnim::Move;
    } else if (b.moving && !b.pre_action_moving) {
        b.anim = BodyAnim::MoveStart;
    } else if (!b.moving && b.pre_action_moving) {
        b.anim = BodyAnim::MoveStop;
    } else {
        b.anim = BodyAnim::Idle;
    }
    b.elapsed = 0;
    b.strike_hit = false;
}

void AnimationController::add_vfx(std::vector<Vfx>& list, SheetId sheet,
                                  std::int32_t lifetime,
                                  std::int32_t frame_duration, Vec2 position,
                                  Vec2 direction, std::int8_t actor) {
    Vfx v;
    v.sheet = sheet;
    v.lifetime = lifetime;
    v.frame_duration = frame_duration;
    v.elapsed = 0;
    v.position = position;
    v.direction = direction;
    v.actor = actor;
    list.push_back(v);
}

SheetId AnimationController::body_sheet(Character character, BodyAnim anim) const {
    switch (character) {
        case Character::Kite:
            switch (anim) {
                case BodyAnim::Idle:
                    return SheetId::KiteIdle;
                case BodyAnim::Move:
                    return SheetId::KiteMoveLoop;
                case BodyAnim::MoveStart:
                case BodyAnim::MoveStop:
                    return SheetId::KiteMoveTransition;
                case BodyAnim::Strike:
                    return SheetId::KiteStrike;
                case BodyAnim::Dash:
                    return SheetId::KiteDash;
                case BodyAnim::Ability:
                    return SheetId::KiteAbility;
                case BodyAnim::GoalWin:
                    return SheetId::KiteGoalWin;
                case BodyAnim::GoalLoss:
                    return SheetId::KiteGoalLoss;
            }
            break;
        case Character::Vale:
            switch (anim) {
                case BodyAnim::Idle:
                    return SheetId::ValeIdle;
                case BodyAnim::Move:
                    return SheetId::ValeMoveLoop;
                case BodyAnim::MoveStart:
                case BodyAnim::MoveStop:
                    return SheetId::ValeMoveTransition;
                case BodyAnim::Strike:
                    return SheetId::ValeStrike;
                case BodyAnim::Dash:
                    return SheetId::ValeDash;
                case BodyAnim::Ability:
                    return SheetId::ValeAbility;
                case BodyAnim::GoalWin:
                    return SheetId::ValeGoalWin;
                case BodyAnim::GoalLoss:
                    return SheetId::ValeGoalLoss;
            }
            break;
        case Character::Bastion:
            switch (anim) {
                case BodyAnim::Idle:
                    return SheetId::BastionIdle;
                case BodyAnim::Move:
                    return SheetId::BastionMoveLoop;
                case BodyAnim::MoveStart:
                case BodyAnim::MoveStop:
                    return SheetId::BastionMoveTransition;
                case BodyAnim::Strike:
                    return SheetId::BastionStrike;
                case BodyAnim::Dash:
                    return SheetId::BastionDash;
                case BodyAnim::Ability:
                    return SheetId::BastionAbility;
                case BodyAnim::GoalWin:
                    return SheetId::BastionGoalWin;
                case BodyAnim::GoalLoss:
                    return SheetId::BastionGoalLoss;
            }
            break;
    }
    return SheetId::KiteIdle;
}

const char* AnimationController::body_anim_name(BodyAnim anim) {
    switch (anim) {
        case BodyAnim::Idle: return "Idle";
        case BodyAnim::MoveStart: return "MoveStart";
        case BodyAnim::Move: return "Move";
        case BodyAnim::MoveStop: return "MoveStop";
        case BodyAnim::Strike: return "Strike";
        case BodyAnim::Dash: return "Dash";
        case BodyAnim::Ability: return "Ability";
        case BodyAnim::GoalWin: return "GoalWin";
        case BodyAnim::GoalLoss: return "GoalLoss";
    }
    return "Unknown";
}

AnimationController::BodyFrame AnimationController::resolve_body_frame(
    Character character, const BodyState& b, int row) const {
    SheetId sheet = body_sheet(character, b.anim);
    const AssetManager::SheetInfo& s = AssetManager::info(sheet);
    int cols = s.columns;
    int frame = row * cols;
    switch (b.anim) {
        case BodyAnim::Idle:
            frame = row * cols + ((b.elapsed / 15) % 4);
            break;
        case BodyAnim::Move:
            frame = row * cols + ((b.elapsed / 10) % 4);
            break;
        case BodyAnim::MoveStart:
            frame = row * cols + std::min(1, b.elapsed / 4);
            break;
        case BodyAnim::MoveStop:
            frame = row * cols + 2 + std::min(1, b.elapsed / 4);
            break;
        case BodyAnim::Strike:
            frame = row * cols + std::min(cols - 1, (b.elapsed * cols) / 18);
            break;
        case BodyAnim::Dash:
            frame = row * cols + std::min(cols - 1, (b.elapsed * cols) / 16);
            break;
        case BodyAnim::Ability:
            frame = row * cols + std::min(cols - 1, (b.elapsed * cols) / 18);
            break;
        case BodyAnim::GoalWin:
        case BodyAnim::GoalLoss:
            frame = row * cols + std::min(cols - 1, (b.elapsed * cols) / 40);
            break;
    }
    return {sheet, frame};
}

AnimationController::BodyDebug AnimationController::body_debug(std::size_t idx) const {
    const BodyState& b = bodies_[idx];
    const PlayerState& p = state_.players[idx];
    int row = cardinal_row(b.facing);
    BodyFrame result = resolve_body_frame(p.character, b, row);
    const AssetManager::SheetInfo& s = AssetManager::info(result.sheet);
    int frame_in_row = result.frame % s.columns;
    return {body_anim_name(b.anim), b.elapsed, result.sheet, frame_in_row,
            s.columns};
}

void AnimationController::update(const GameState& state,
                                 const StepEvents& events) {
    state_ = state;

    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        BodyState& b = bodies_[i];
        const PlayerState& p = state.players[i];

        std::uint64_t speed =
            isqrt64(static_cast<std::uint64_t>(length_sq(p.velocity)));
        if (b.moving && speed <= 10) {
            b.moving = false;
        } else if (!b.moving && speed >= 18) {
            b.moving = true;
        }

        if (b.anim == BodyAnim::Idle || b.anim == BodyAnim::Move ||
            b.anim == BodyAnim::MoveStart || b.anim == BodyAnim::MoveStop) {
            b.facing = p.facing;
        }

        ++b.elapsed;

        if (b.anim == BodyAnim::Idle && b.moving) {
            b.anim = BodyAnim::MoveStart;
            b.elapsed = 0;
        } else if (b.anim == BodyAnim::MoveStart && b.elapsed >= 8) {
            b.anim = BodyAnim::Move;
            b.elapsed = 0;
        } else if (b.anim == BodyAnim::Move && !b.moving) {
            b.anim = BodyAnim::MoveStop;
            b.elapsed = 0;
        } else if (b.anim == BodyAnim::MoveStop && b.elapsed >= 8) {
            b.anim = BodyAnim::Idle;
            b.elapsed = 0;
        } else if (b.anim == BodyAnim::Strike && b.elapsed >= 18) {
            resolve_action_end(b);
        } else if (b.anim == BodyAnim::Dash && b.elapsed >= 16) {
            resolve_action_end(b);
        } else if (b.anim == BodyAnim::Ability && b.elapsed >= 18) {
            resolve_action_end(b);
        } else if ((b.anim == BodyAnim::GoalWin || b.anim == BodyAnim::GoalLoss) &&
                   b.elapsed >= 40) {
            resolve_action_end(b);
        }
    }

    for (auto& pe : persistent_) {
        if (pe.active) {
            ++pe.elapsed;
            if (pe.elapsed >= pe.display_duration) {
                pe.active = false;
            }
        }
    }

    for (auto& v : trails_) {
        ++v.elapsed;
    }
    while (!trails_.empty() && trails_.front().elapsed >= trails_.front().lifetime) {
        trails_.erase(trails_.begin());
    }

    for (auto& v : overlays_) {
        ++v.elapsed;
    }
    while (!overlays_.empty() &&
           overlays_.front().elapsed >= overlays_.front().lifetime) {
        overlays_.erase(overlays_.begin());
    }

    for (std::uint8_t i = 0; i < events.count; ++i) {
        const SimulationEvent& ev = events.events[i];
        switch (ev.type) {
            case SimulationEventType::GoalScored: {
                std::int8_t winner = ev.actor;
                std::int8_t loser = (winner == 0) ? 1 : 0;
                BodyState& wb = bodies_[winner];
                wb.anim = BodyAnim::GoalWin;
                wb.elapsed = 0;
                wb.pre_action_moving = wb.moving;
                wb.facing = {0, -kFixedScale};

                BodyState& lb = bodies_[loser];
                lb.anim = BodyAnim::GoalLoss;
                lb.elapsed = 0;
                lb.pre_action_moving = lb.moving;
                lb.facing = {0, -kFixedScale};

                add_vfx(overlays_, SheetId::GoalBurst, 72, 6, ev.position,
                        ev.direction, winner);
                break;
            }
            case SimulationEventType::StrikeStarted: {
                if (ev.actor < 0 || ev.actor > 1) break;
                BodyState& b = bodies_[ev.actor];
                if (b.anim != BodyAnim::GoalWin && b.anim != BodyAnim::GoalLoss) {
                    b.anim = BodyAnim::Strike;
                    b.elapsed = 0;
                    b.pre_action_moving = b.moving;
                    b.strike_hit = false;
                    b.facing = ev.direction;
                }
                break;
            }
            case SimulationEventType::StrikeHit: {
                if (ev.actor < 0 || ev.actor > 1) break;
                BodyState& b = bodies_[ev.actor];
                b.strike_hit = true;
                Vec2 sweep_pos = state.players[ev.actor].position;
                add_vfx(trails_, SheetId::StrikeSweep, 18, 3, sweep_pos,
                        ev.direction, ev.actor);
                add_vfx(overlays_, SheetId::CoreImpact, 18, 3, ev.position,
                        ev.direction, ev.actor);
                break;
            }
            case SimulationEventType::DashStarted: {
                if (ev.actor < 0 || ev.actor > 1) break;
                BodyState& b = bodies_[ev.actor];
                if (b.anim != BodyAnim::GoalWin && b.anim != BodyAnim::GoalLoss &&
                    b.anim != BodyAnim::Strike && b.anim != BodyAnim::Ability) {
                    b.anim = BodyAnim::Dash;
                    b.elapsed = 0;
                    b.pre_action_moving = b.moving;
                    b.facing = ev.direction;
                }
                add_vfx(trails_, SheetId::DashTrail, 18, 3, ev.position,
                        ev.direction, ev.actor);
                break;
            }
            case SimulationEventType::AbilityActivated: {
                if (ev.actor < 0 || ev.actor > 1) break;
                BodyState& b = bodies_[ev.actor];
                if (b.anim != BodyAnim::GoalWin && b.anim != BodyAnim::GoalLoss &&
                    b.anim != BodyAnim::Strike) {
                    b.anim = BodyAnim::Ability;
                    b.elapsed = 0;
                    b.pre_action_moving = b.moving;
                    b.facing = ev.direction;
                }

                PersistentEffect& pe = persistent_[ev.actor];
                pe.active = true;
                pe.kind = ev.effect_kind;
                pe.position = ev.position;
                pe.direction = ev.direction;
                pe.elapsed = 0;
                pe.follow_player = (ev.effect_kind == EffectKind::Jetstep);
                switch (ev.effect_kind) {
                    case EffectKind::Jetstep:
                        pe.active_duration = kJetstepDuration;
                        break;
                    case EffectKind::AnchorWell:
                        pe.active_duration = kAnchorDuration;
                        break;
                    case EffectKind::PulseGate:
                        pe.active_duration = kGateDuration;
                        break;
                    default:
                        pe.active_duration = 0;
                        break;
                }
                pe.display_duration = pe.active_duration + 12;
                break;
            }
            case SimulationEventType::CoreBounce: {
                add_vfx(overlays_, SheetId::CoreImpact, 18, 3, ev.position,
                        ev.direction, ev.actor);
                break;
            }
            default:
                break;
        }
    }
}

void AnimationController::draw_entities(HDC hdc, const AssetManager& assets,
                                        int court_x, int court_y,
                                        int court_width, int court_height) const {
    // Kickoff pulse behind the world effects.
    if (state_.phase == Phase::Kickoff) {
        int cx = court_x + world_to_screen_x(state_.core.position.x, court_width);
        int cy = court_y + world_to_screen_y(state_.core.position.y, court_height);
        int r = world_radius_to_screen(3 * kFixedScale, court_width);
        int frame = (state_.tick / 4) % 8;
        if (!assets.draw_sprite(hdc, SheetId::KickoffPulse, frame, cx, cy, r * 2,
                                r * 2, 0.0f, 1.0f)) {
            draw_hollow_circle(hdc, cx, cy, r, RGB(0, 200, 255), PS_DOT, 2);
            draw_hollow_circle(hdc, cx, cy, r * 2 / 3, RGB(0, 150, 200), PS_SOLID, 1);
        }
    }

    // Persistent ability effects.
    for (std::size_t i = 0; i < persistent_.size(); ++i) {
        draw_persistent(hdc, assets, court_x, court_y, court_width, court_height, i);
    }

    // Trail/sweep VFX (behind players).
    for (const auto& v : trails_) {
        draw_vfx(hdc, assets, court_x, court_y, court_width, court_height, v);
    }

    // Players.
    for (std::size_t i = 0; i < state_.players.size(); ++i) {
        draw_body(hdc, assets, court_x, court_y, court_width, court_height, i);
    }

    // Core.
    {
        int cx = court_x + world_to_screen_x(state_.core.position.x, court_width);
        int cy = court_y + world_to_screen_y(state_.core.position.y, court_height);
        int cr = world_radius_to_screen(kCoreRadius, court_width);
        int frame = (state_.tick / 4) % 8;
        if (!assets.draw_sprite(hdc, SheetId::CoreSpin, frame, cx, cy, cr * 3,
                                cr * 3, 0.0f, 1.0f)) {
            draw_circle(hdc, cx, cy, cr, kCoreColor, RGB(200, 200, 200));
        }
    }

    // Impact/goal overlays.
    for (const auto& v : overlays_) {
        draw_vfx(hdc, assets, court_x, court_y, court_width, court_height, v);
    }
}

void AnimationController::draw_body(HDC hdc, const AssetManager& assets, int court_x,
                                    int court_y, int court_width, int court_height,
                                    std::size_t player_idx) const {
    const BodyState& b = bodies_[player_idx];
    const PlayerState& p = state_.players[player_idx];
    int px = court_x + world_to_screen_x(p.position.x, court_width);
    int py = court_y + world_to_screen_y(p.position.y, court_height);
    int pr = world_radius_to_screen(kPlayerRadius, court_width);
    int row = cardinal_row(b.facing);

    // Try the real body sheet for the current animation state first. When it
    // is available, the authored frames provide the motion, so no procedural
    // squash/stretch/lunge transforms are applied.
    BodyFrame result = resolve_body_frame(p.character, b, row);
    if (assets.has(result.sheet)) {
        int draw_w = static_cast<int>(pr * kPlayerBodySpriteSizeMultiplier);
        int draw_h = draw_w;
        if (assets.draw_sprite(hdc, result.sheet, result.frame, px, py, draw_w,
                               draw_h, 0.0f, 1.0f)) {
            return;
        }
    }

    // Fallback: idle sheet with the original procedural pose transforms.
    SheetId sheet = body_sheet(p.character, BodyAnim::Idle);

    if (!assets.has(sheet)) {
        COLORREF fill = (player_idx == 0) ? kPlayer1Color : kPlayer2Color;
        draw_circle(hdc, px, py, pr, fill, RGB(240, 240, 240));
        int fx = px + (b.facing.x * pr) / kFixedScale;
        int fy = py - (b.facing.y * pr) / kFixedScale;
        draw_line(hdc, px, py, fx, fy, RGB(255, 255, 0), 2);
        return;
    }

    const AssetManager::SheetInfo& s = assets.info(sheet);
    int cols = s.columns;
    int frame = row * cols;

    switch (b.anim) {
        case BodyAnim::Idle:
            frame = row * cols + ((b.elapsed / 15) % 4);
            break;
        case BodyAnim::MoveStart:
            frame = row * cols + ((b.elapsed / 2) % 4);
            break;
        case BodyAnim::Move:
            frame = row * cols + ((b.elapsed / 10) % 4);
            break;
        case BodyAnim::MoveStop:
            frame = row * cols + ((2 + b.elapsed / 2) % 4);
            break;
        default:
            // Strike, Dash, Ability, and goal poses use a stable idle frame.
            break;
    }

    int draw_x = px;
    int draw_y = py;
    int draw_w = static_cast<int>(pr * kPlayerBodySpriteSizeMultiplier);
    int draw_h = draw_w;

    switch (b.anim) {
        case BodyAnim::Strike: {
            double t = static_cast<double>(b.elapsed) / 18.0;
            double pulse = std::sin(t * kPi);
            double offset = pr * kStrikeLungeDistance * pulse;
            draw_x = px + static_cast<int>(static_cast<double>(b.facing.x) *
                                           offset / kFixedScale);
            draw_y = py - static_cast<int>(static_cast<double>(b.facing.y) *
                                           offset / kFixedScale);
            draw_w = static_cast<int>(draw_w * (1.0 + kStrikeStretchX * pulse));
            draw_h = static_cast<int>(draw_h * (1.0 - kStrikeSquashY * pulse));
            break;
        }
        case BodyAnim::Dash: {
            double t = static_cast<double>(b.elapsed) / 16.0;
            double pulse = std::sin(t * kPi);
            double offset = pr * kDashLeanDistance * pulse;
            draw_x = px + static_cast<int>(static_cast<double>(b.facing.x) *
                                           offset / kFixedScale);
            draw_y = py - static_cast<int>(static_cast<double>(b.facing.y) *
                                           offset / kFixedScale);
            draw_w = static_cast<int>(draw_w * (1.0 + kDashStretchX * pulse));
            draw_h = static_cast<int>(draw_h * (1.0 - kDashSquashY * pulse));
            break;
        }
        case BodyAnim::Ability: {
            double t = static_cast<double>(b.elapsed) / 18.0;
            double pulse = std::sin(t * kPi);
            double lunge = pr * kAbilityLungeDistance * pulse;
            int lift = static_cast<int>(pr * kAbilityLiftDistance * pulse);
            draw_x = px + static_cast<int>(static_cast<double>(b.facing.x) *
                                           lunge / kFixedScale);
            draw_y = py - static_cast<int>(static_cast<double>(b.facing.y) *
                                           lunge / kFixedScale) -
                     lift;
            int new_size = static_cast<int>(draw_w * (1.0 + kAbilityPulseScale * pulse));
            draw_w = new_size;
            draw_h = new_size;
            break;
        }
        case BodyAnim::GoalWin: {
            double t = static_cast<double>(b.elapsed) / 40.0;
            double pulse = std::sin(t * kPi);
            draw_y = py - static_cast<int>(pr * kGoalWinJumpDistance * pulse);
            int new_size = static_cast<int>(draw_w * (1.0 + kGoalWinPulseScale * pulse));
            draw_w = new_size;
            draw_h = new_size;
            break;
        }
        case BodyAnim::GoalLoss: {
            double t = static_cast<double>(b.elapsed) / 40.0;
            double pulse = std::sin(t * kPi);
            draw_y = py + static_cast<int>(pr * kGoalLossSlumpDistance * pulse);
            int new_size = static_cast<int>(draw_w * (1.0 - kGoalLossPulseScale * pulse));
            draw_w = new_size;
            draw_h = new_size;
            break;
        }
        default:
            break;
    }

    if (assets.draw_sprite(hdc, sheet, frame, draw_x, draw_y, draw_w, draw_h,
                           0.0f, 1.0f)) {
        return;
    }

    COLORREF fill = (player_idx == 0) ? kPlayer1Color : kPlayer2Color;
    draw_circle(hdc, draw_x, draw_y, pr, fill, RGB(240, 240, 240));
    int fx = draw_x + (b.facing.x * pr) / kFixedScale;
    int fy = draw_y - (b.facing.y * pr) / kFixedScale;
    draw_line(hdc, draw_x, draw_y, fx, fy, RGB(255, 255, 0), 2);
}

void AnimationController::draw_persistent(HDC hdc, const AssetManager& assets,
                                          int court_x, int court_y,
                                          int court_width, int court_height,
                                          std::size_t player_idx) const {
    const PersistentEffect& pe = persistent_[player_idx];
    if (!pe.active) return;

    SheetId sheet;
    int base_r = 0;
    switch (pe.kind) {
        case EffectKind::Jetstep:
            sheet = SheetId::JetstepFx;
            base_r = world_radius_to_screen(kPlayerRadius, court_width);
            break;
        case EffectKind::AnchorWell:
            sheet = SheetId::AnchorWellFx;
            base_r = world_radius_to_screen(kAnchorRadius, court_width);
            break;
        case EffectKind::PulseGate:
            sheet = SheetId::PulseGateFx;
            base_r = world_radius_to_screen(kGateRadius, court_width);
            break;
        default:
            return;
    }

    Vec2 pos = pe.follow_player ? state_.players[player_idx].position : pe.position;
    int ex = court_x + world_to_screen_x(pos.x, court_width);
    int ey = court_y + world_to_screen_y(pos.y, court_height);

    int frame = 0;
    if (pe.elapsed < 12) {
        frame = pe.elapsed / 2;
    } else if (pe.elapsed < pe.active_duration) {
        frame = 6 + ((pe.elapsed - 12) / 2) % 6;
    } else {
        int f = 12 + (pe.elapsed - pe.active_duration) / 2;
        frame = (f < 17) ? f : 17;
    }

    float alpha = 1.0f;
    if (pe.elapsed >= pe.active_duration) {
        int fade = pe.display_duration - pe.active_duration;
        if (fade > 0) {
            alpha = 1.0f - static_cast<float>(pe.elapsed - pe.active_duration) / fade;
            if (alpha < 0.0f) alpha = 0.0f;
        }
    }

    float angle = 0.0f;
    int dest_w = base_r * 2;
    if (pe.kind == EffectKind::Jetstep) {
        dest_w = base_r * 3;
    } else if (pe.kind == EffectKind::AnchorWell) {
        dest_w = static_cast<int>(base_r * 2 * kAnchorWellArtScale);
    }

    if (assets.draw_sprite(hdc, sheet, frame, ex, ey, dest_w, dest_w, angle,
                           alpha)) {
        if (pe.kind == EffectKind::AnchorWell) {
            // Keep the true radius visible as a subtle dotted ring.
            draw_hollow_circle(hdc, ex, ey, base_r, RGB(0, 200, 200), PS_DOT, 1);
        }
        return;
    }
    draw_persistent_fallback(hdc, pe, court_x, court_y, court_width, court_height,
                             player_idx);
}

void AnimationController::draw_persistent_fallback(HDC hdc, const PersistentEffect& pe,
                                                  int court_x, int court_y,
                                                  int court_width, int court_height,
                                                  std::size_t player_idx) const {
    Vec2 pos = pe.follow_player ? state_.players[player_idx].position : pe.position;
    int ex = court_x + world_to_screen_x(pos.x, court_width);
    int ey = court_y + world_to_screen_y(pos.y, court_height);

    if (pe.kind == EffectKind::AnchorWell) {
        int er = world_radius_to_screen(kAnchorRadius, court_width);
        double anim = (pe.elapsed % 20) / 20.0;
        draw_hollow_circle(hdc, ex, ey, er, RGB(0, 200, 200), PS_DOT, 2);
        draw_hollow_circle(hdc, ex, ey,
                          static_cast<int>(er * (0.5 + 0.5 * anim)),
                          RGB(0, 150, 180), PS_SOLID, 1);
    } else if (pe.kind == EffectKind::PulseGate) {
        int er = world_radius_to_screen(kGateRadius, court_width);
        double pulse = (pe.elapsed % 15) / 15.0;
        int pulse_r = static_cast<int>(er * (0.8 + 0.4 * pulse));
        draw_hollow_circle(hdc, ex, ey, pulse_r, RGB(255, 180, 0), PS_SOLID, 3);
        draw_hollow_circle(hdc, ex, ey, static_cast<int>(er * 0.6),
                          RGB(255, 150, 0), PS_DOT, 1);
    } else if (pe.kind == EffectKind::Jetstep) {
        const PlayerState& p = state_.players[player_idx];
        int px = court_x + world_to_screen_x(p.position.x, court_width);
        int py = court_y + world_to_screen_y(p.position.y, court_height);
        int pr = world_radius_to_screen(kPlayerRadius, court_width) + 8;
        draw_hollow_circle(hdc, px, py, pr, RGB(0, 200, 255), PS_DOT, 2);
        double trail = (pe.elapsed % 10) / 10.0;
        draw_hollow_circle(hdc, px, py,
                          static_cast<int>(pr * (1.0 + 0.5 * trail)),
                          RGB(0, 150, 200), PS_SOLID, 1);
    }
}

void AnimationController::draw_vfx(HDC hdc, const AssetManager& assets, int court_x,
                                   int court_y, int court_width, int court_height,
                                   const Vfx& vfx) const {
    int cx = court_x + world_to_screen_x(vfx.position.x, court_width);
    int cy = court_y + world_to_screen_y(vfx.position.y, court_height);

    const AssetManager::SheetInfo& s = assets.info(vfx.sheet);
    int frame = vfx.elapsed / vfx.frame_duration;
    if (frame >= s.frames) frame = s.frames - 1;
    float alpha = 1.0f - static_cast<float>(vfx.elapsed) / vfx.lifetime;
    if (alpha < 0.0f) alpha = 0.0f;
    float angle = 0.0f;

    int dest_w = 0;
    switch (vfx.sheet) {
        case SheetId::CoreImpact:
            dest_w = 2 * world_radius_to_screen(2 * kFixedScale, court_width);
            break;
        case SheetId::StrikeSweep:
        case SheetId::DashTrail:
            dest_w = 2 * world_radius_to_screen(kPlayerRadius * 3, court_width);
            angle = direction_angle_deg(vfx.direction);
            break;
        case SheetId::GoalBurst:
            dest_w = 2 * world_radius_to_screen(4 * kFixedScale, court_width);
            break;
        default:
            dest_w = 2 * world_radius_to_screen(kCoreRadius, court_width);
            break;
    }

    if (assets.draw_sprite(hdc, vfx.sheet, frame, cx, cy, dest_w, dest_w, angle,
                          alpha)) {
        return;
    }

    double life = static_cast<double>(vfx.elapsed) / vfx.lifetime;
    COLORREF color = RGB(255, 255, 100);
    if (vfx.sheet == SheetId::GoalBurst) {
        color = (vfx.actor == 0) ? RGB(100, 200, 100) : RGB(200, 100, 100);
    } else if (vfx.actor == 0) {
        color = kPlayer1Color;
    } else if (vfx.actor == 1) {
        color = kPlayer2Color;
    }

    int r = static_cast<int>(dest_w * (1.0 - life) / 2.0);
    if (r < 2) r = 2;

    if (vfx.sheet == SheetId::DashTrail) {
        int len = r;
        int dx = cx - (vfx.direction.x * len) / kFixedScale;
        int dy = cy + (vfx.direction.y * len) / kFixedScale;
        draw_line(hdc, cx, cy, dx, dy, color, 3);
        draw_hollow_circle(hdc, cx, cy,
                          world_radius_to_screen(kPlayerRadius, court_width),
                          color, PS_DOT, 2);
    } else if (vfx.sheet == SheetId::GoalBurst) {
        draw_hollow_circle(hdc, cx, cy, r, color, PS_SOLID, 4);
    } else {
        draw_hollow_circle(hdc, cx, cy, r, color, PS_SOLID, 2);
        if (vfx.direction.x != 0 || vfx.direction.y != 0) {
            int dx = cx + (vfx.direction.x * r) / kFixedScale;
            int dy = cy - (vfx.direction.y * r) / kFixedScale;
            draw_line(hdc, cx, cy, dx, dy, color, 2);
        }
    }
}

void draw_court_lines(HDC hdc, const AssetManager& assets, int court_x,
                      int court_y, int court_width, int court_height) {
    int cx = court_x + court_width / 2;
    int cy = court_y + court_height / 2;
    draw_line(hdc, cx, court_y, cx, court_y + court_height, kCourtLines, 2);
    draw_line(hdc, court_x, cy, court_x + court_width, cy, kCourtLines, 1);

    int goal_depth_screen = world_to_screen_x(2 * kFixedScale, court_width);
    int goal_top = court_y + world_to_screen_y(kGoalBottom, court_height);
    int goal_bottom = court_y + world_to_screen_y(kGoalTop, court_height);
    int court_right = court_x + court_width;

    bool arena_loaded = assets.has(SheetId::Arena);
    if (!arena_loaded) {
        RECT left_goal = {court_x, goal_top, court_x + goal_depth_screen,
                          goal_bottom};
        draw_rect(hdc, left_goal, kGoalArea, kGoalArea);
        RECT right_goal = {court_right - goal_depth_screen, goal_top, court_right,
                           goal_bottom};
        draw_rect(hdc, right_goal, kGoalArea, kGoalArea);
    }

    int r_screen = world_radius_to_screen(kCoreRadius, court_width);
    int wall_top =
        court_y + world_to_screen_y(kFieldHeight - kCoreRadius, court_height);
    int wall_bottom = court_y + world_to_screen_y(kCoreRadius, court_height);
    int left_wall = court_x + r_screen;
    int right_wall = court_x + court_width - r_screen;

    draw_line(hdc, left_wall, wall_top, right_wall, wall_top, kWallColor, 3);
    draw_line(hdc, left_wall, wall_bottom, right_wall, wall_bottom, kWallColor,
              3);
    draw_line(hdc, left_wall, wall_top, left_wall, goal_top, kWallColor, 3);
    draw_line(hdc, left_wall, goal_bottom, left_wall, wall_bottom, kWallColor,
              3);
    draw_line(hdc, right_wall, wall_top, right_wall, goal_top, kWallColor, 3);
    draw_line(hdc, right_wall, goal_bottom, right_wall, wall_bottom, kWallColor,
              3);
}

}  // namespace pulse
