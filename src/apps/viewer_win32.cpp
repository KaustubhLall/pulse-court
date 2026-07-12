#include "pulse_sim.hpp"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include <deque>

namespace pulse {

namespace {
using std::fmod;

// Global state
static Simulation* g_sim = nullptr;
static std::array<Character, 2> g_chars = {Character::Kite, Character::Vale};
static bool g_reset_requested = false;
static bool g_quit_requested = false;
static bool g_manual_pause = false;
static bool g_has_focus = true;

// Edge-triggered input state
static bool g_key_state[256] = {};
static bool g_key_pressed[256] = {};

// Timing
static LARGE_INTEGER g_perf_freq = {};
static LARGE_INTEGER g_last_time = {};
static double g_accumulated_time = 0.0;
static double g_render_accumulated_time = 0.0;
static double g_last_render_time = 0.0;
static constexpr double kSimDt = 1.0 / kSimHz;  // 120 Hz
static constexpr double kRenderDt = 1.0 / 60.0;  // 60 FPS
static constexpr double kMaxCatchUp = 0.1;  // Max 100ms catch-up

// FPS tracking
static std::deque<double> g_frame_times;
static constexpr std::size_t kFpsHistory = 60;
static bool g_first_frame_rendered = false;

// Visual effects
struct VisualEffect {
    SimulationEventType type = SimulationEventType::None;
    std::int8_t actor = -1;
    EffectKind effect_kind = EffectKind::None;
    Vec2 position{};
    Vec2 direction{};
    double age = 0.0;
    double duration = 0.5;  // Default 500ms
};

static std::deque<VisualEffect> g_effects;
static constexpr std::size_t kMaxEffects = 96;

// Action trace
struct TraceEntry {
    std::uint32_t tick = 0;
    std::int8_t actor = -1;
    SimulationEventType type = SimulationEventType::None;
    const char* description = "";
    std::string reasoning;
};

static std::deque<TraceEntry> g_trace;
static constexpr std::size_t kMaxTraceEntries = 8;
static constexpr std::uint32_t kBounceTraceCoalesceTicks = kSimHz / 4;

// Window constants
const wchar_t kClassName[] = L"PulseCourtViewer";
const int kClientWidth = 1440;
const int kClientHeight = 840;

// Layout constants
const int kSidebarWidth = 400;
const int kCardPadding = 12;
const int kCardSpacing = 8;
const int kFooterHeight = 40;
const int kCourtPadding = 20;

// Color palette
const COLORREF kCourtBackground = RGB(15, 35, 25);
const COLORREF kCourtLines = RGB(70, 100, 70);
const COLORREF kGoalArea = RGB(35, 20, 20);
const COLORREF kWallColor = RGB(220, 220, 220);
const COLORREF kPlayer1Color = RGB(60, 100, 220);
const COLORREF kPlayer2Color = RGB(220, 80, 60);
const COLORREF kCoreColor = RGB(240, 240, 240);
const COLORREF kPanelBackground = RGB(25, 30, 40);
const COLORREF kPanelBorder = RGB(70, 80, 100);
const COLORREF kTextMain = RGB(220, 220, 220);
const COLORREF kTextDim = RGB(150, 150, 150);
const COLORREF kCooldownBar = RGB(100, 150, 200);
const COLORREF kCooldownEmpty = RGB(50, 60, 70);

// Coordinate conversion
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

// Drawing primitives
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

void draw_hollow_circle(HDC hdc, int x, int y, int r, COLORREF stroke,
                        int style = PS_SOLID, int width = 2) {
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
               int width = 2) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
    MoveToEx(hdc, x1, y1, nullptr);
    LineTo(hdc, x2, y2);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

void draw_rect(HDC hdc, RECT rc, COLORREF fill, COLORREF stroke = RGB(0, 0, 0)) {
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
               int height = 14, const char* font = "Consolas") {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    HFONT hfont = CreateFontA(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                              DEFAULT_PITCH | FF_SWISS,
                              font ? font : "Consolas");
    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, hfont));
    TextOutA(hdc, x, y, text, static_cast<int>(strlen(text)));
    SelectObject(hdc, old_font);
    DeleteObject(hfont);
}

void draw_cooldown_bar(HDC hdc, int x, int y, int width, int height,
                       std::int32_t current, std::int32_t max_val,
                       const char* label) {
    // Background
    RECT bg = {x, y, x + width, y + height};
    draw_rect(hdc, bg, kCooldownEmpty, kCooldownEmpty);

    // Fill
    if (max_val > 0) {
        double ratio = 1.0 - static_cast<double>(current) / max_val;
        int fill_width = static_cast<int>(width * ratio);
        if (fill_width > 0) {
            RECT fill = {x, y, x + fill_width, y + height};
            draw_rect(hdc, fill, kCooldownBar, kCooldownBar);
        }
    }

    // Label
    char buf[64];
    snprintf(buf, sizeof(buf), "%s: %d/%d", label, current, max_val);
    draw_text(hdc, x, y + height + 2, buf, kTextDim, 12);
}

// Helper functions
const char* phase_name(Phase p) {
    switch (p) {
        case Phase::Kickoff: return "Kickoff";
        case Phase::Live: return "Live";
        case Phase::MatchOver: return "MatchOver";
    }
    return "?";
}

const char* event_type_name(SimulationEventType type) {
    switch (type) {
        case SimulationEventType::StrikeStarted: return "Strike";
        case SimulationEventType::StrikeHit: return "Strike Hit";
        case SimulationEventType::DashStarted: return "Dash";
        case SimulationEventType::AbilityActivated: return "Ability";
        case SimulationEventType::CoreBounce: return "Bounce";
        case SimulationEventType::GoalScored: return "GOAL";
        default: return "?";
    }
}

const char* ability_name(EffectKind kind) {
    switch (kind) {
        case EffectKind::Jetstep: return "Jetstep";
        case EffectKind::AnchorWell: return "Anchor Well";
        case EffectKind::PulseGate: return "Pulse Gate";
        default: return "None";
    }
}

const char* ability_description(Character character) {
    switch (character) {
        case Character::Kite: return "Jetstep: speed burst; next strike adds power";
        case Character::Vale: return "Anchor Well: pulls nearby Core toward its center";
        case Character::Bastion: return "Pulse Gate: core-only reflector placed ahead";
        default: return "Unknown ability";
    }
}

const char* character_ability_name(Character character) {
    switch (character) {
        case Character::Kite: return "Jetstep";
        case Character::Vale: return "Anchor Well";
        case Character::Bastion: return "Pulse Gate";
        default: return "None";
    }
}

const char* actor_name(std::int8_t actor) {
    if (actor == -1) return "World";
    if (actor == 0) return "P1";
    if (actor == 1) return "P2";
    return "?";
}

// Input handling
void clear_edge_triggers() {
    memset(g_key_pressed, 0, sizeof(g_key_pressed));
}

FrameInput poll_left_input() {
    FrameInput in;
    int mx = 0;
    int my = 0;

    // Movement (held)
    if (g_key_state[0x41]) mx -= 1;  // A
    if (g_key_state[0x44]) mx += 1;  // D
    if (g_key_state[0x57]) my += 1;  // W
    if (g_key_state[0x53]) my -= 1;  // S

    in.move_x = static_cast<std::int8_t>(mx);
    in.move_y = static_cast<std::int8_t>(my);
    in.aim_x = in.move_x;
    in.aim_y = in.move_y;

    // Edge-triggered actions
    if (g_key_pressed[0x46]) in.buttons |= ButtonStrike;   // F
    if (g_key_pressed[0x47]) in.buttons |= ButtonAbility;  // G
    if (g_key_pressed[0x48]) in.buttons |= ButtonDash;     // H

    return in;
}

FrameInput poll_right_input() {
    FrameInput in;
    int mx = 0;
    int my = 0;

    // Movement (held) - I/J/K/L layout
    if (g_key_state[0x4A]) mx -= 1;  // J (left)
    if (g_key_state[0x4C]) mx += 1;  // L (right)
    if (g_key_state[0x49]) my += 1;  // I (up)
    if (g_key_state[0x4B]) my -= 1;  // K (down)

    in.move_x = static_cast<std::int8_t>(mx);
    in.move_y = static_cast<std::int8_t>(my);
    in.aim_x = in.move_x;
    in.aim_y = in.move_y;

    // Edge-triggered actions
    if (g_key_pressed[0x55]) in.buttons |= ButtonStrike;   // U
    if (g_key_pressed[0x4F]) in.buttons |= ButtonAbility;  // O
    if (g_key_pressed[0x50]) in.buttons |= ButtonDash;     // P

    return in;
}

// Effect management
void add_effect(const SimulationEvent& event) {
    if (g_effects.size() >= kMaxEffects) {
        g_effects.pop_front();
    }

    VisualEffect ve;
    ve.type = event.type;
    ve.actor = event.actor;
    ve.effect_kind = event.effect_kind;
    ve.position = event.position;
    ve.direction = event.direction;
    ve.age = 0.0;

    // Set duration based on type
    switch (event.type) {
        case SimulationEventType::StrikeStarted:
            ve.duration = 0.2;
            break;
        case SimulationEventType::StrikeHit:
            ve.duration = 0.15;
            break;
        case SimulationEventType::DashStarted:
            ve.duration = 0.25;
            break;
        case SimulationEventType::AbilityActivated:
            ve.duration = 0.4;
            break;
        case SimulationEventType::CoreBounce:
            ve.duration = 0.1;
            break;
        case SimulationEventType::GoalScored:
            ve.duration = 0.5;
            break;
        default:
            ve.duration = 0.3;
    }

    g_effects.push_back(ve);
}

void update_effects(double dt) {
    for (auto& ve : g_effects) {
        ve.age += dt;
    }

    // Remove expired effects
    while (!g_effects.empty() && g_effects.front().age >= g_effects.front().duration) {
        g_effects.pop_front();
    }
}

void add_trace_entry(const StepEvents& events) {
    for (std::uint8_t i = 0; i < events.count; ++i) {
        const auto& ev = events.events[i];

        // World-wall contacts can occur in rapid succession. Keep one
        // up-to-date row for that background telemetry so player actions do
        // not immediately disappear from the inspection trace.
        if (ev.type == SimulationEventType::CoreBounce && ev.actor == -1 &&
            !g_trace.empty()) {
            TraceEntry& last = g_trace.back();
            if (last.type == SimulationEventType::CoreBounce &&
                last.actor == -1 &&
                events.tick - last.tick < kBounceTraceCoalesceTicks) {
                last.tick = events.tick;
                continue;
            }
        }

        if (g_trace.size() >= kMaxTraceEntries) {
            g_trace.pop_front();
        }

        TraceEntry te;
        te.tick = events.tick;
        te.actor = ev.actor;
        te.type = ev.type;
        te.description = event_type_name(ev.type);

        g_trace.push_back(te);
    }
}

// Rendering
void render_court(HDC hdc, int court_x, int court_y, int court_width, int court_height) {
    const GameState& s = g_sim->state();

    // Court background
    RECT court_rc = {court_x, court_y, court_x + court_width, court_y + court_height};
    draw_rect(hdc, court_rc, kCourtBackground, kCourtBackground);

    // Center line
    int cx = court_x + court_width / 2;
    int cy = court_y + court_height / 2;
    draw_line(hdc, cx, court_y, cx, court_y + court_height, kCourtLines, 2);
    draw_line(hdc, court_x, cy, court_x + court_width, cy, kCourtLines, 1);

    // Goals - shallow zones at left/right short sides (about 2 world units deep)
    int goal_depth_screen = world_to_screen_x(2 * kFixedScale, court_width);
    int goal_top = court_y + world_to_screen_y(kGoalBottom, court_height);
    int goal_bottom = court_y + world_to_screen_y(kGoalTop, court_height);
    RECT left_goal = {court_x, goal_top, court_x + goal_depth_screen, goal_bottom};
    draw_rect(hdc, left_goal, kGoalArea, kGoalArea);

    int court_right = court_x + court_width;
    RECT right_goal = {court_right - goal_depth_screen, goal_top, court_right, goal_bottom};
    draw_rect(hdc, right_goal, kGoalArea, kGoalArea);

    // Walls
    int r_screen = world_radius_to_screen(kCoreRadius, court_width);
    int wall_top = court_y + world_to_screen_y(kFieldHeight, court_height) - r_screen;
    int wall_bottom = court_y + world_to_screen_y(0, court_height) + r_screen;
    int left_wall = court_x + world_to_screen_x(0, court_width) + r_screen;
    int right_wall = court_x + world_to_screen_x(kFieldWidth, court_width) - r_screen;

    draw_line(hdc, court_x, wall_top, court_x + court_width, wall_top, kWallColor, 3);
    draw_line(hdc, court_x, wall_bottom, court_x + court_width, wall_bottom, kWallColor, 3);
    draw_line(hdc, left_wall, court_y, left_wall, goal_top, kWallColor, 3);
    draw_line(hdc, left_wall, goal_bottom, left_wall, court_y + court_height, kWallColor, 3);
    draw_line(hdc, right_wall, court_y, right_wall, goal_top, kWallColor, 3);
    draw_line(hdc, right_wall, goal_bottom, right_wall, court_y + court_height, kWallColor, 3);

    // Draw persistent effects from game state
    for (const auto& p : s.players) {
        if (p.effect_ticks == 0) continue;

        int ex = court_x + world_to_screen_x(p.effect_position.x, court_width);
        int ey = court_y + world_to_screen_y(p.effect_position.y, court_height);

        if (p.effect_kind == EffectKind::AnchorWell) {
            int er = world_radius_to_screen(kAnchorRadius, court_width);
            // Animated concentric rings
            double anim = (p.effect_ticks % 20) / 20.0;
            draw_hollow_circle(hdc, ex, ey, er, RGB(0, 200, 200), PS_DOT, 2);
            draw_hollow_circle(hdc, ex, ey, static_cast<int>(er * (0.5 + 0.5 * anim)),
                              RGB(0, 150, 180), PS_SOLID, 1);
        } else if (p.effect_kind == EffectKind::PulseGate) {
            int er = world_radius_to_screen(kGateRadius, court_width);
            // Pulsing hexagon-like effect
            double pulse = (p.effect_ticks % 15) / 15.0;
            int pulse_r = static_cast<int>(er * (0.8 + 0.4 * pulse));
            draw_hollow_circle(hdc, ex, ey, pulse_r, RGB(255, 180, 0), PS_SOLID, 3);
            draw_hollow_circle(hdc, ex, ey, static_cast<int>(er * 0.6), RGB(255, 150, 0), PS_DOT, 1);
        } else if (p.effect_kind == EffectKind::Jetstep) {
            int px = court_x + world_to_screen_x(p.position.x, court_width);
            int py = court_y + world_to_screen_y(p.position.y, court_height);
            int pr = world_radius_to_screen(kPlayerRadius, court_width) + 8;
            // Cyan pulse ring
            draw_hollow_circle(hdc, px, py, pr, RGB(0, 200, 255), PS_DOT, 2);
            // Trailing afterimage effect
            double trail = (p.effect_ticks % 10) / 10.0;
            draw_hollow_circle(hdc, px, py, static_cast<int>(pr * (1.0 + 0.5 * trail)),
                              RGB(0, 150, 200), PS_SOLID, 1);
        }
    }

    // Draw transient visual effects
    for (const auto& ve : g_effects) {
        double life = ve.age / ve.duration;
        if (life >= 1.0) continue;

        int ex = court_x + world_to_screen_x(ve.position.x, court_width);
        int ey = court_y + world_to_screen_y(ve.position.y, court_height);
        double alpha = 1.0 - life;

        switch (ve.type) {
            case SimulationEventType::StrikeStarted: {
                // Directional wedge/arc
                int arc_r = world_radius_to_screen(kStrikeReach, court_width);
                COLORREF arc_color = RGB(255, 255, 100);
                draw_hollow_circle(hdc, ex, ey, static_cast<int>(arc_r * alpha),
                                  arc_color, PS_SOLID, 2);
                // Direction indicator
                if (ve.direction.x != 0 || ve.direction.y != 0) {
                    int dx = ex + (ve.direction.x * arc_r) / kFixedScale;
                    int dy = ey - (ve.direction.y * arc_r) / kFixedScale;
                    draw_line(hdc, ex, ey, dx, dy, arc_color, 2);
                }
                break;
            }
            case SimulationEventType::StrikeHit: {
                // Core starburst
                int burst_r = world_radius_to_screen(kCoreRadius, court_width) * 3;
                COLORREF burst_color = RGB(255, 200, 50);
                draw_hollow_circle(hdc, ex, ey, static_cast<int>(burst_r * alpha),
                                  burst_color, PS_SOLID, 2);
                draw_hollow_circle(hdc, ex, ey, static_cast<int>(burst_r * 0.5 * alpha),
                                  burst_color, PS_DOT, 1);
                break;
            }
            case SimulationEventType::DashStarted: {
                // Directional afterimage streak
                int streak_len = world_radius_to_screen(kDashSpeed * 3, court_width);
                COLORREF streak_color = (ve.actor == 0) ? RGB(100, 150, 255) : RGB(255, 150, 100);
                if (ve.direction.x != 0 || ve.direction.y != 0) {
                    int dx = ex - (ve.direction.x * streak_len) / kFixedScale;
                    int dy = ey + (ve.direction.y * streak_len) / kFixedScale;
                    draw_line(hdc, ex, ey, dx, dy, streak_color, 3);
                }
                draw_hollow_circle(hdc, ex, ey, world_radius_to_screen(kPlayerRadius, court_width),
                                  streak_color, PS_DOT, 2);
                break;
            }
            case SimulationEventType::AbilityActivated: {
                // Ability-specific visual
                COLORREF ability_color = RGB(0, 255, 200);
                if (ve.effect_kind == EffectKind::Jetstep) {
                    ability_color = RGB(0, 200, 255);
                } else if (ve.effect_kind == EffectKind::AnchorWell) {
                    ability_color = RGB(0, 200, 200);
                } else if (ve.effect_kind == EffectKind::PulseGate) {
                    ability_color = RGB(255, 180, 0);
                }
                int effect_r = world_radius_to_screen(kAnchorRadius, court_width);
                draw_hollow_circle(hdc, ex, ey, static_cast<int>(effect_r * alpha),
                                  ability_color, PS_SOLID, 2);
                break;
            }
            case SimulationEventType::CoreBounce: {
                // Radial impact sparks
                int spark_r = world_radius_to_screen(kCoreRadius, court_width) * 2;
                COLORREF spark_color = RGB(255, 255, 200);
                draw_hollow_circle(hdc, ex, ey, static_cast<int>(spark_r * alpha),
                                  spark_color, PS_DOT, 2);
                break;
            }
            case SimulationEventType::GoalScored: {
                // Goal-side flash
                COLORREF goal_color = (ve.actor == 0) ? RGB(100, 200, 100) : RGB(200, 100, 100);
                int flash_r = court_width / 4;
                draw_hollow_circle(hdc, ex, ey, static_cast<int>(flash_r * alpha),
                                  goal_color, PS_SOLID, 4);
                break;
            }
            default:
                break;
        }
    }

    // Core
    int core_x = court_x + world_to_screen_x(s.core.position.x, court_width);
    int core_y = court_y + world_to_screen_y(s.core.position.y, court_height);
    int core_r = world_radius_to_screen(kCoreRadius, court_width);
    draw_circle(hdc, core_x, core_y, core_r, kCoreColor, RGB(200, 200, 200));

    // Players
    for (std::size_t i = 0; i < s.players.size(); ++i) {
        const auto& p = s.players[i];
        int px = court_x + world_to_screen_x(p.position.x, court_width);
        int py = court_y + world_to_screen_y(p.position.y, court_height);
        int pr = world_radius_to_screen(kPlayerRadius, court_width);
        COLORREF fill = (i == 0) ? kPlayer1Color : kPlayer2Color;
        draw_circle(hdc, px, py, pr, fill, RGB(240, 240, 240));

        // Facing indicator
        int fx = px + (p.facing.x * pr) / kFixedScale;
        int fy = py - (p.facing.y * pr) / kFixedScale;
        draw_line(hdc, px, py, fx, fy, RGB(255, 255, 0), 2);
    }
}

void render_sidebar(HDC hdc, int sidebar_x, int sidebar_y, int sidebar_width, int /*sidebar_height*/) {
    const GameState& s = g_sim->state();
    int y = sidebar_y + kCardPadding;

    // Match card
    RECT match_card = {sidebar_x + kCardPadding, y,
                       sidebar_x + sidebar_width - kCardPadding, y + 140};
    draw_rect(hdc, match_card, kPanelBackground, kPanelBorder);
    y += kCardPadding;

    draw_text(hdc, match_card.left + kCardPadding, y, "MATCH STATUS", kTextMain, 16);
    y += 20;

    char score_buf[64];
    snprintf(score_buf, sizeof(score_buf), "Score: %d - %d", s.score[0], s.score[1]);
    draw_text(hdc, match_card.left + kCardPadding, y, score_buf, kTextMain, 14);
    y += 18;

    char phase_buf[64];
    snprintf(phase_buf, sizeof(phase_buf), "Phase: %s", phase_name(s.phase));
    draw_text(hdc, match_card.left + kCardPadding, y, phase_buf, kTextDim, 12);
    y += 16;

    char tick_buf[64];
    snprintf(tick_buf, sizeof(tick_buf), "Tick: %u", s.tick);
    draw_text(hdc, match_card.left + kCardPadding, y, tick_buf, kTextDim, 12);
    y += 16;

    // Clock info
    char clock_buf[128];
    snprintf(clock_buf, sizeof(clock_buf), "SIM 120 Hz / VIEW 60 FPS");
    draw_text(hdc, match_card.left + kCardPadding, y, clock_buf, kTextDim, 12);
    y += 16;

    // Policy cadence status
    if (is_decision_tick(s.tick)) {
        draw_text(hdc, match_card.left + kCardPadding, y, "POLICY: boundary now", RGB(0, 200, 200), 12);
    } else {
        std::int32_t ticks_until_decision = kDecisionIntervalTicks - (s.tick % kDecisionIntervalTicks);
        char policy_buf[64];
        snprintf(policy_buf, sizeof(policy_buf), "POLICY: %d ticks until refresh", ticks_until_decision);
        draw_text(hdc, match_card.left + kCardPadding, y, policy_buf, kTextDim, 12);
    }
    y += 16;

    // Pause status
    const char* pause_status = "Running";
    if (!g_has_focus) pause_status = "Unfocused (paused)";
    else if (g_manual_pause) pause_status = "Manual pause";
    draw_text(hdc, match_card.left + kCardPadding, y, pause_status, RGB(255, 200, 100), 12);

    y = match_card.bottom + kCardSpacing;

    // Player cards
    for (std::size_t i = 0; i < s.players.size(); ++i) {
        const auto& p = s.players[i];
        RECT player_card = {sidebar_x + kCardPadding, y,
                           sidebar_x + sidebar_width - kCardPadding, y + 184};
        COLORREF card_color = (i == 0) ? RGB(40, 50, 70) : RGB(70, 40, 40);
        draw_rect(hdc, player_card, card_color, kPanelBorder);
        y += kCardPadding;

        const char* player_label = (i == 0) ? "PLAYER 1 (Blue)" : "PLAYER 2 (Orange)";
        draw_text(hdc, player_card.left + kCardPadding, y, player_label, kTextMain, 14);
        y += 18;

        char char_buf[64];
        snprintf(char_buf, sizeof(char_buf), "Character: %s", character_name(p.character));
        draw_text(hdc, player_card.left + kCardPadding, y, char_buf, kTextDim, 12);
        y += 16;

        // Controls
        const char* controls = (i == 0) ? "Controls: WASD move, F Strike, G Ability, H Dash"
                                        : "Controls: IJKL move, U Strike, O Ability, P Dash";
        draw_text(hdc, player_card.left + kCardPadding, y, controls, kTextDim, 11);
        y += 16;

        // Always show ability info
        const char* abil_name = character_ability_name(p.character);
        const char* abil_desc = ability_description(p.character);
        draw_text(hdc, player_card.left + kCardPadding, y, abil_name, RGB(0, 200, 200), 12);
        y += 14;
        draw_text(hdc, player_card.left + kCardPadding, y, abil_desc, kTextDim, 11);
        y += 16;

        // Active status
        if (p.effect_kind != EffectKind::None) {
            char status_buf[64];
            snprintf(status_buf, sizeof(status_buf), "Status: Active (%d ticks)", p.effect_ticks);
            draw_text(hdc, player_card.left + kCardPadding, y, status_buf, RGB(0, 255, 150), 11);
        } else if (p.ability_cooldown == 0) {
            draw_text(hdc, player_card.left + kCardPadding, y, "Status: Ready", RGB(0, 255, 150), 11);
        } else {
            char cd_buf[64];
            snprintf(cd_buf, sizeof(cd_buf), "Status: Cooldown (%d)", p.ability_cooldown);
            draw_text(hdc, player_card.left + kCardPadding, y, cd_buf, kTextDim, 11);
        }
        y += 16;

        // Cooldown bars
        int bar_x = player_card.left + kCardPadding;
        int bar_y = y;
        int bar_w = 120;
        int bar_h = 10;

        draw_cooldown_bar(hdc, bar_x, bar_y, bar_w, bar_h, p.strike_cooldown, kStrikeCooldown, "Strike");
        bar_y += 24;
        draw_cooldown_bar(hdc, bar_x, bar_y, bar_w, bar_h, p.dash_cooldown, kDashCooldown, "Dash");
        bar_y += 24;
        draw_cooldown_bar(hdc, bar_x, bar_y, bar_w, bar_h, p.ability_cooldown, kAbilityCooldown, "Ability");

        y = player_card.bottom + kCardSpacing;
    }

    // Action Trace card
    RECT trace_card = {sidebar_x + kCardPadding, y,
                      sidebar_x + sidebar_width - kCardPadding, y + 160};
    draw_rect(hdc, trace_card, kPanelBackground, kPanelBorder);
    y += kCardPadding;

    draw_text(hdc, trace_card.left + kCardPadding, y, "ACTION TRACE", kTextMain, 14);
    y += 20;

    if (g_trace.empty()) {
        draw_text(hdc, trace_card.left + kCardPadding, y, "No events yet", kTextDim, 12);
    } else {
        for (const auto& entry : g_trace) {
            char trace_buf[128];
            snprintf(trace_buf, sizeof(trace_buf), "T%u %s %s",
                    entry.tick, actor_name(entry.actor), entry.description);
            draw_text(hdc, trace_card.left + kCardPadding, y, trace_buf, kTextDim, 11);
            y += 14;
            if (!entry.reasoning.empty()) {
                draw_text(hdc, trace_card.left + kCardPadding + 10, y, entry.reasoning.c_str(), RGB(150, 150, 100), 10);
                y += 12;
            }
        }
    }

    y = trace_card.bottom + kCardSpacing;

    // Policy Inspector card
    RECT policy_card = {sidebar_x + kCardPadding, y,
                       sidebar_x + sidebar_width - kCardPadding, y + 100};
    draw_rect(hdc, policy_card, kPanelBackground, kPanelBorder);
    y += kCardPadding;

    draw_text(hdc, policy_card.left + kCardPadding, y, "POLICY INSPECTOR", kTextMain, 14);
    y += 20;

    draw_text(hdc, policy_card.left + kCardPadding, y, "Input Source: Keyboard", kTextDim, 12);
    y += 16;

    // Show latest action if available
    if (!g_trace.empty()) {
        const auto& latest = g_trace.back();
        char action_buf[64];
        snprintf(action_buf, sizeof(action_buf), "Latest Action: %s", latest.description);
        draw_text(hdc, policy_card.left + kCardPadding, y, action_buf, kTextDim, 12);
        y += 16;
    }

    // Show reasoning or static reserved message
    if (!g_trace.empty() && !g_trace.back().reasoning.empty()) {
        draw_text(hdc, policy_card.left + kCardPadding, y, g_trace.back().reasoning.c_str(), RGB(150, 150, 100), 11);
    } else {
        draw_text(hdc, policy_card.left + kCardPadding, y, "Reasoning: reserved for policy adapter",
                  RGB(150, 150, 100), 11);
    }
}

void render_footer(HDC hdc, int court_x, int court_y, int court_height) {
    const char* footer = "R reset | Space pause | Esc quit | Aim follows movement/last facing";
    int footer_y = court_y + court_height + 8;
    draw_text(hdc, court_x, footer_y, footer, kTextDim, 12);
}

void render(HWND hwnd) {
    if (!g_sim) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    HDC hdc_win = GetDC(hwnd);
    HDC hdc = CreateCompatibleDC(hdc_win);
    HBITMAP bmp = CreateCompatibleBitmap(hdc_win, w, h);
    HBITMAP old_bmp = static_cast<HBITMAP>(SelectObject(hdc, bmp));

    // Background
    RECT bg = {0, 0, w, h};
    FillRect(hdc, &bg, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    // Layout - pin sidebar to right edge, center aspect-preserved court in left play region
    int sidebar_x = w - kSidebarWidth;
    int available_width = sidebar_x - 2 * kCourtPadding;
    int available_height = h - kFooterHeight - 2 * kCourtPadding;

    int court_width, court_height;
    if (available_width <= 0 || available_height <= 0) {
        // Fallback for tiny windows
        court_width = 100;
        court_height = 100;
    } else {
        // Maintain 38:22 aspect ratio
        double aspect_ratio = static_cast<double>(kFieldWidth) / kFieldHeight;
        double available_aspect = static_cast<double>(available_width) / available_height;

        if (available_aspect > aspect_ratio) {
            // Height is limiting factor
            court_height = available_height;
            court_width = static_cast<int>(court_height * aspect_ratio);
        } else {
            // Width is limiting factor
            court_width = available_width;
            court_height = static_cast<int>(court_width / aspect_ratio);
        }
    }

    int court_x = kCourtPadding + (available_width - court_width) / 2;
    int court_y = kCourtPadding + (available_height - court_height) / 2;
    int sidebar_y = 0;

    // Render components
    render_court(hdc, court_x, court_y, court_width, court_height);
    render_sidebar(hdc, sidebar_x, sidebar_y, kSidebarWidth, h);
    render_footer(hdc, court_x, court_y, court_height);

    // FPS display
    if (!g_frame_times.empty()) {
        double avg_time = 0.0;
        for (double t : g_frame_times) {
            avg_time += t;
        }
        avg_time /= g_frame_times.size();
        double fps = (avg_time > 0.0) ? (1.0 / avg_time) : 0.0;

        char fps_buf[64];
        snprintf(fps_buf, sizeof(fps_buf), "FPS: %.1f", fps);
        draw_text(hdc, 10, h - 20, fps_buf, RGB(100, 255, 100), 12);
    }

    BitBlt(hdc_win, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(hdc);
    ReleaseDC(hwnd, hdc_win);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
            // Set minimum tracking dimensions to keep sidebar cards legible
            mmi->ptMinTrackSize.x = 1200;  // Minimum width for 400px sidebar + court
            mmi->ptMinTrackSize.y = 880;   // Minimum height for all cards
            return 0;
        }

        case WM_KEYDOWN:
            if (wparam < 256) {
                bool just_pressed = !g_key_state[wparam];
                g_key_state[wparam] = true;
                if (just_pressed) {
                    g_key_pressed[wparam] = true;
                    // Only trigger global actions on just-pressed transition
                    if (wparam == VK_ESCAPE) {
                        g_quit_requested = true;
                        return 0;
                    }
                    if (wparam == 'R') {
                        g_reset_requested = true;
                        return 0;
                    }
                    if (wparam == VK_SPACE) {
                        g_manual_pause = !g_manual_pause;
                        return 0;
                    }
                }
            }
            break;

        case WM_KEYUP:
            if (wparam < 256) {
                g_key_state[wparam] = false;
                g_key_pressed[wparam] = false;
            }
            break;

        case WM_SETFOCUS:
            g_has_focus = true;
            break;

        case WM_KILLFOCUS:
            g_has_focus = false;
            // Clear all input on focus loss
            memset(g_key_state, 0, sizeof(g_key_state));
            memset(g_key_pressed, 0, sizeof(g_key_pressed));
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

}  // namespace

}  // namespace pulse

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    using namespace pulse;

    // Initialize timing
    QueryPerformanceFrequency(&g_perf_freq);
    QueryPerformanceCounter(&g_last_time);
    g_last_render_time = static_cast<double>(g_last_time.QuadPart) / g_perf_freq.QuadPart;

    // Parse command line.
    std::string cmd(lpCmdLine ? lpCmdLine : "");
    std::istringstream iss(cmd);
    std::vector<std::string> args;
    std::string a;
    while (iss >> a) args.push_back(a);
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--left" && i + 1 < args.size()) {
            if (!parse_character(args[++i].c_str(), g_chars[0])) return 1;
        } else if (args[i] == "--right" && i + 1 < args.size()) {
            if (!parse_character(args[++i].c_str(), g_chars[1])) return 1;
        }
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kClassName;
    if (!RegisterClassW(&wc)) return 1;

    RECT rc = {0, 0, kClientWidth, kClientHeight};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowW(
        kClassName, L"Pulse Court Viewer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr,
        nullptr, hInstance, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    Simulation sim(g_chars);
    g_sim = &sim;

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        if (g_quit_requested) {
            running = false;
            break;
        }
        if (g_reset_requested) {
            sim.reset(g_chars);
            g_effects.clear();
            g_trace.clear();
            g_accumulated_time = 0.0;
            g_render_accumulated_time = 0.0;
            g_manual_pause = false;
            memset(g_key_state, 0, sizeof(g_key_state));
            memset(g_key_pressed, 0, sizeof(g_key_pressed));
            g_reset_requested = false;
        }

        // Timing
        LARGE_INTEGER current_time;
        QueryPerformanceCounter(&current_time);
        double delta = static_cast<double>(current_time.QuadPart - g_last_time.QuadPart) / g_perf_freq.QuadPart;
        g_last_time = current_time;

        // Fixed-step simulation accumulator (120 Hz, unchanged)
        if (g_has_focus && !g_manual_pause) {
            g_accumulated_time += delta;
            if (g_accumulated_time > kMaxCatchUp) {
                g_accumulated_time = kMaxCatchUp;  // Cap catch-up
            }

            while (g_accumulated_time >= kSimDt) {
                std::array<FrameInput, 2> inputs = {poll_left_input(), poll_right_input()};
                StepEvents events;
                (void)sim.step(inputs, &events);

                // Process events
                for (std::uint8_t i = 0; i < events.count; ++i) {
                    add_effect(events.events[i]);
                }
                add_trace_entry(events);

                clear_edge_triggers();
                g_accumulated_time -= kSimDt;
            }
        } else {
            // When paused/unfocused, just reset accumulator
            g_accumulated_time = 0.0;
        }

        // Update visual effects
        update_effects(delta);

        // Render accumulator (60 FPS cap)
        g_render_accumulated_time += delta;
        bool should_render = !g_first_frame_rendered || g_render_accumulated_time >= kRenderDt;

        if (should_render) {
            // Track FPS only for actual rendered frames
            double current_time_sec = static_cast<double>(current_time.QuadPart) / g_perf_freq.QuadPart;
            double time_since_last_render = current_time_sec - g_last_render_time;
            g_last_render_time = current_time_sec;
            if (time_since_last_render > 0.0) {
                g_frame_times.push_back(time_since_last_render);
                if (g_frame_times.size() > kFpsHistory) {
                    g_frame_times.pop_front();
                }
            }

            // Render
            render(hwnd);
            g_first_frame_rendered = true;

            // Drop excess after stall to sub-frame remainder
            g_render_accumulated_time = fmod(g_render_accumulated_time, kRenderDt);
        }

        // Small sleep to yield CPU
        Sleep(1);
    }

    DestroyWindow(hwnd);
    return 0;
}
