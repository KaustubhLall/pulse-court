#define NOMINMAX

#include "pulse_sim.hpp"
#include "fixed_math.hpp"
#include "viewer_assets.hpp"
#include "viewer_monitor.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <vector>

namespace pulse {

namespace {

constexpr int kClientWidth = 1300;
constexpr int kClientHeight = 800;
constexpr int kMinClientWidth = 1180;
constexpr int kMinClientHeight = 780;
constexpr int kSidebarWidth = 280;
constexpr int kCardPadding = 8;
constexpr int kCardSpacing = 16;
constexpr int kFooterHeight = 52;
constexpr int kCourtPadding = 16;
constexpr double kSimHz = 120.0;
constexpr double kSimDt = 1.0 / kSimHz;
constexpr double kRenderDt = 1.0 / 60.0;
constexpr double kMaxCatchUp = 0.1;
constexpr std::size_t kFpsHistory = 30;
constexpr std::size_t kMaxTraceEntries = 12;
constexpr std::int32_t kBounceTraceCoalesceTicks = 2;
const wchar_t kClassName[] = L"PulseCourtViewer";

}  // namespace

// Globals
static HWND g_hwnd = nullptr;
static bool g_quit_requested = false;
static bool g_has_focus = true;
static bool g_manual_pause = false;
static bool g_reset_requested = false;

static bool g_key_state[256] = {};
static bool g_key_pressed[256] = {};

static Simulation* g_sim = nullptr;
static AssetManager* g_assets = nullptr;
static AnimationController g_anim;

static std::array<Character, 2> g_chars = {Character::Kite, Character::Vale};
static bool g_setup_mode = false;
static bool g_start_match = false;
static int g_active_player = 0;
static bool g_mouse_clicked = false;
static int g_mouse_x = 0;
static int g_mouse_y = 0;

static bool g_passive = false;
static bool g_use_monitor_placement = false;
static std::wstring g_monitor_name;
static std::string g_monitor_name_narrow;

static std::array<FrameInput, 2> g_input = {};
static std::array<std::uint8_t, 2> g_pending_buttons = {};

static bool g_gallery_mode = false;
static int g_gallery_sheet_index = -1;
static int g_gallery_frame = 0;

static bool g_show_hitboxes = false;
static bool g_show_anim_debug = false;
static bool g_bot_active = false;
static double g_sim_speed = 1.0;
static bool g_step_requested = false;

static int g_min_track_width = 0;
static int g_min_track_height = 0;

double g_accumulated_time = 0.0;
static double g_render_accumulated_time = 0.0;
static std::deque<double> g_frame_times;

struct TraceEntry {
    std::int32_t tick;
    char text[96];
    COLORREF color;
};
static std::deque<TraceEntry> g_trace;

static std::string narrow_arg(const wchar_t* w) {
    if (!w) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr,
                                 nullptr);
    if (len <= 0) return {};
    std::string s;
    s.resize(static_cast<std::size_t>(len));
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

static void clear_edge_triggers() {
    std::memset(g_key_pressed, 0, sizeof(g_key_pressed));
}

const char* phase_name(Phase phase) {
    switch (phase) {
        case Phase::Kickoff: return "Kickoff";
        case Phase::Live: return "Live";
        case Phase::MatchOver: return "Match Over";
    }
    return "Unknown";
}

const char* event_type_name(SimulationEventType type) {
    switch (type) {
        case SimulationEventType::CoreBounce: return "Bounce";
        case SimulationEventType::StrikeStarted: return "Strike";
        case SimulationEventType::StrikeHit: return "Hit";
        case SimulationEventType::AbilityActivated: return "Ability";
        case SimulationEventType::DashStarted: return "Dash";
        case SimulationEventType::GoalScored: return "Goal";
    }
    return "Event";
}

EffectKind effect_kind_for_character(Character character) {
    switch (character) {
        case Character::Kite: return EffectKind::Jetstep;
        case Character::Vale: return EffectKind::AnchorWell;
        case Character::Bastion: return EffectKind::PulseGate;
    }
    return EffectKind::None;
}

const char* effect_kind_name(EffectKind kind) {
    switch (kind) {
        case EffectKind::Jetstep: return "Jetstep";
        case EffectKind::AnchorWell: return "Anchor Well";
        case EffectKind::PulseGate: return "Pulse Gate";
        case EffectKind::None: return "None";
    }
    return "Unknown";
}

const char* effect_kind_description(EffectKind kind) {
    switch (kind) {
        case EffectKind::Jetstep: return "Speed burst";
        case EffectKind::AnchorWell: return "Deploy slow well";
        case EffectKind::PulseGate: return "Deploy reflect gate";
        case EffectKind::None: return "";
    }
    return "";
}

const char* character_ability_name(Character character) {
    return effect_kind_name(effect_kind_for_character(character));
}

const char* actor_name(std::int8_t actor, const char* fallback = "World") {
    if (actor < 0) return fallback;
    if (actor < static_cast<std::int8_t>(g_sim->state().players.size())) {
        return character_name(g_sim->state().players[actor].character);
    }
    return fallback;
}

static void add_trace_entry(std::int32_t tick, const char* text,
                            COLORREF color = kTextDim) {
    TraceEntry e;
    e.tick = tick;
    e.color = color;
    snprintf(e.text, sizeof(e.text), "%s", text);
    g_trace.push_back(e);
    if (g_trace.size() > kMaxTraceEntries) {
        g_trace.pop_front();
    }
}

static void poll_input() {
    g_input[0].move_x = 0;
    g_input[0].move_y = 0;
    g_input[0].buttons = ButtonNone;
    g_input[1].move_x = 0;
    g_input[1].move_y = 0;
    g_input[1].buttons = ButtonNone;
    g_step_requested = false;

    auto set_move = [](FrameInput& in, bool up, bool down, bool left, bool right) {
        if (up) in.move_y += 1;
        if (down) in.move_y -= 1;
        if (left) in.move_x -= 1;
        if (right) in.move_x += 1;
        in.aim_x = in.move_x;
        in.aim_y = in.move_y;
    };

    set_move(g_input[0], g_key_state['W'], g_key_state['S'], g_key_state['A'],
             g_key_state['D']);
    set_move(g_input[1], g_key_state['I'], g_key_state['K'], g_key_state['J'],
             g_key_state['L']);

    if (g_key_pressed['F']) g_pending_buttons[0] |= ButtonStrike;
    if (g_key_pressed['G']) g_pending_buttons[0] |= ButtonAbility;
    if (g_key_pressed['H']) g_pending_buttons[0] |= ButtonDash;
    if (g_key_pressed['U']) g_pending_buttons[1] |= ButtonStrike;
    if (g_key_pressed['O']) g_pending_buttons[1] |= ButtonAbility;
    if (g_key_pressed['P']) g_pending_buttons[1] |= ButtonDash;

    if (g_key_pressed['R']) g_reset_requested = true;
    if (g_key_pressed[VK_SPACE]) {
        g_manual_pause = !g_manual_pause;
        g_key_pressed[VK_SPACE] = false;
    }
    if (g_key_pressed[VK_ESCAPE]) g_quit_requested = true;

    if (g_key_pressed['N'] && g_manual_pause) {
        g_step_requested = true;
        g_key_pressed['N'] = false;
    }
    if (g_key_pressed[VK_F1]) {
        g_show_hitboxes = !g_show_hitboxes;
        g_key_pressed[VK_F1] = false;
    }
    if (g_key_pressed[VK_F2]) {
        g_show_anim_debug = !g_show_anim_debug;
        g_key_pressed[VK_F2] = false;
    }
    if (g_key_pressed['B']) {
        g_bot_active = !g_bot_active;
        g_key_pressed['B'] = false;
    }
    if (g_key_pressed[VK_OEM_4]) {
        g_sim_speed = (g_sim_speed <= 0.5 ? 0.25 : 0.5);
        g_key_pressed[VK_OEM_4] = false;
    }
    if (g_key_pressed[VK_OEM_6]) {
        g_sim_speed = (g_sim_speed >= 0.5 ? 1.0 : 0.5);
        g_key_pressed[VK_OEM_6] = false;
    }

    clear_edge_triggers();
}

static FrameInput compute_bot_input(const GameState& state) {
    FrameInput in{};
    const PlayerState& p = state.players[1];
    const CoreState& core = state.core;

    std::int64_t dx = core.position.x - p.position.x;
    std::int64_t dy = core.position.y - p.position.y;
    std::int64_t threshold = kFixedScale / 4;

    auto sign = [](std::int64_t v, std::int64_t t) -> std::int8_t {
        if (v > t) return 1;
        if (v < -t) return -1;
        return 0;
    };

    in.move_x = sign(dx, threshold);
    in.move_y = sign(dy, threshold);
    in.aim_x = in.move_x;
    in.aim_y = in.move_y;

    std::int64_t dist_sq = dx * dx + dy * dy;
    std::uint64_t dist = isqrt64(static_cast<std::uint64_t>(dist_sq));

    if (p.strike_cooldown == 0 &&
        dist <= static_cast<std::uint64_t>(kStrikeReach + kCoreRadius)) {
        in.buttons |= ButtonStrike;
    }
    if (p.dash_cooldown == 0 && dist > static_cast<std::uint64_t>(8 * kFixedScale)) {
        in.buttons |= ButtonDash;
    }
    if (p.ability_cooldown == 0 &&
        dist <= static_cast<std::uint64_t>(6 * kFixedScale)) {
        in.buttons |= ButtonAbility;
    }

    return in;
}

static void advance_tick(Simulation& sim, const std::array<FrameInput, 2>& in) {
    StepEvents events;
    StepResult result = sim.step(in, &events);
    g_anim.update(sim.state(), events);

    static std::array<std::int32_t, 3> last_bounce_tick = {-1000, -1000, -1000};
    for (std::uint8_t i = 0; i < events.count; ++i) {
        const SimulationEvent& e = events.events[i];
        const std::int32_t tick = static_cast<std::int32_t>(sim.state().tick);
        if (e.type == SimulationEventType::CoreBounce) {
            int idx = e.actor + 1;
            if (tick - last_bounce_tick[idx] >= kBounceTraceCoalesceTicks) {
                last_bounce_tick[idx] = tick;
                char buf[96];
                if (e.effect_kind == EffectKind::PulseGate) {
                    snprintf(buf, sizeof(buf), "t%d: Core bounce off %s gate", tick,
                             actor_name(e.actor));
                } else if (e.actor == -1) {
                    snprintf(buf, sizeof(buf), "t%d: Core bounce (wall)", tick);
                } else {
                    snprintf(buf, sizeof(buf), "t%d: Core bounce by %s", tick,
                             actor_name(e.actor));
                }
                add_trace_entry(tick, buf, kTextDim);
            }
        } else if (e.type == SimulationEventType::StrikeStarted) {
            char buf[96];
            snprintf(buf, sizeof(buf), "t%d: Strike by %s", tick, actor_name(e.actor));
            add_trace_entry(tick, buf, kTextDim);
        } else if (e.type == SimulationEventType::StrikeHit) {
            char buf[96];
            snprintf(buf, sizeof(buf), "t%d: Strike hit by %s", tick, actor_name(e.actor));
            add_trace_entry(tick, buf, kTextDim);
        } else if (e.type == SimulationEventType::DashStarted) {
            char buf[96];
            snprintf(buf, sizeof(buf), "t%d: Dash by %s", tick, actor_name(e.actor));
            add_trace_entry(tick, buf, kTextDim);
        } else if (e.type == SimulationEventType::AbilityActivated) {
            char buf[96];
            snprintf(buf, sizeof(buf), "t%d: %s uses %s", tick, actor_name(e.actor),
                     effect_kind_name(e.effect_kind));
            add_trace_entry(tick, buf, kTextDim);
        } else if (e.type == SimulationEventType::GoalScored) {
            char buf[96];
            snprintf(buf, sizeof(buf), "t%d: Goal by %s", tick, actor_name(e.actor, "Team"));
            add_trace_entry(tick, buf, kTextMain);
        }
    }

    if (result.match_finished) {
        g_manual_pause = true;
    }
}

static void render_sidebar(HDC hdc, int x, int y, int w, int h, int fps) {
    RECT panel = {x, y, x + w, y + h};
    draw_rect(hdc, panel, kPanelBackground, kPanelBorder);

    if (!g_sim) return;
    const GameState& s = g_sim->state();

    char buf[128];
    int yy = y + 10;
    draw_text(hdc, x + 12, yy, "Pulse Court", kTextMain, 18);
    yy += 24;

    int match_h = 64;
    RECT match_card = {x + 12, yy, x + w - 12, yy + match_h};
    draw_rect(hdc, match_card, kPanelBackground, kPanelBorder);
    snprintf(buf, sizeof(buf), "Match Status");
    draw_text(hdc, x + 18, yy + 6, buf, kTextMain, 14);
    snprintf(buf, sizeof(buf), "Phase: %s | Tick: %d  Score: %d - %d",
             phase_name(s.phase), s.tick, s.score[0], s.score[1]);
    draw_text(hdc, x + 18, yy + 22, buf, kTextDim, 12);
    snprintf(buf, sizeof(buf), "Sim: 120 Hz | Display: %d FPS", fps);
    draw_text(hdc, x + 18, yy + 36, buf, kTextDim, 12);
    int next = static_cast<int>(((s.tick / kDecisionIntervalTicks) + 1) *
                                kDecisionIntervalTicks);
    int until = next - static_cast<int>(s.tick);
    snprintf(buf, sizeof(buf),
             "Policy: 10 Hz | Next boundary: t%d (%d ticks)", next, until);
    draw_text(hdc, x + 18, yy + 50, buf, kTextDim, 12);
    yy += match_h + kCardSpacing;

    for (std::size_t i = 0; i < s.players.size(); ++i) {
        const PlayerState& p = s.players[i];
        COLORREF card_fill = (i == 0) ? kPlayer1CardFill : kPlayer2CardFill;
        COLORREF card_border = (i == 0) ? kPlayer1Color : kPlayer2Color;
        int card_h = 108;
        int card_x = x + 12;
        int card_y = yy;
        int card_w = w - 24;
        RECT card = {card_x, card_y, card_x + card_w, card_y + card_h};
        draw_rect(hdc, card, card_fill, card_border);

        snprintf(buf, sizeof(buf), "%s P%d", character_name(p.character),
                 static_cast<int>(i + 1));
        draw_text(hdc, card_x + 8, card_y + 8, buf, kTextMain, 14);

        const char* ability = character_ability_name(p.character);
        snprintf(buf, sizeof(buf), "%s - %s", ability,
                 effect_kind_description(effect_kind_for_character(p.character)));
        draw_text(hdc, card_x + 8, card_y + 24, buf, kTextDim, 12);

        if (g_assets) {
            (void)g_assets->draw_sprite(hdc, SheetId::AbilityIcons,
                                        static_cast<int>(p.character),
                                        card_x + card_w - 20, card_y + 20, 24,
                                        24, 0.0f, 1.0f);
        }

        draw_cooldown_bar(hdc, card_x + 8, card_y + 40, card_w - 16, 10,
                          p.strike_cooldown, kStrikeCooldown, "Strike");
        draw_cooldown_bar(hdc, card_x + 8, card_y + 62, card_w - 16, 10,
                          p.ability_cooldown, kAbilityCooldown, "Ability");
        draw_cooldown_bar(hdc, card_x + 8, card_y + 84, card_w - 16, 10,
                          p.dash_cooldown, kDashCooldown, "Dash");

        yy += card_h + kCardSpacing;
    }

    draw_text(hdc, x + 12, yy, "Action Trace", kTextMain, 14);
    yy += 14;
    int trace_y = yy;
    for (const auto& e : g_trace) {
        draw_text(hdc, x + 12, yy, e.text, e.color, 12);
        yy += 14;
    }
    yy = trace_y + static_cast<int>(kMaxTraceEntries * 14);
    yy += kCardSpacing;

    draw_text(hdc, x + 12, yy, "Controls", kTextMain, 14);
    yy += 14;
    draw_text(hdc, x + 12, yy, "WASD / IJKL move", kTextDim, 12);
    yy += 14;
    draw_text(hdc, x + 12, yy, "F/U Strike  G/O Ability  H/P Dash", kTextDim, 12);
    yy += 14;
    draw_text(hdc, x + 12, yy, "R Reset  Space Pause  Esc Quit", kTextDim, 12);
    yy += 14;
    draw_text(hdc, x + 12, yy,
              "F1 hitbox  F2 anim  B bot  N step  [ ] speed", kTextDim, 12);
    yy += 14;

    int policy_h = h - (yy - y) - 10;
    if (policy_h > 0) {
        RECT policy = {x + 12, yy, x + w - 12, yy + policy_h};
        draw_rect(hdc, policy, kPanelBackground, kPanelBorder);
        draw_text(hdc, x + 18, yy + 8, "Policy Inspector", kTextMain, 14);
        draw_text(hdc, x + 18, yy + 28,
                  "Reserved for policy reasoning and decision boundary data.",
                  kTextDim, 12);
        draw_text(hdc, x + 18, yy + 46,
                  "No reasoning output is currently displayed.", kTextDim, 12);
    }
}

static void render_footer(HDC hdc, int x, int y) {
    int yy = y;
    char buf[256];
    if (g_sim) {
        const GameState& s = g_sim->state();
        snprintf(buf, sizeof(buf),
                 "P1: %s    P2: %s    Last Touch: %s    Regulation: %d",
                 character_name(s.players[0].character),
                 character_name(s.players[1].character),
                 s.last_touch == 0 ? "P1" : s.last_touch == 1 ? "P2" : "None",
                 s.regulation_remaining);
    } else {
        buf[0] = '\0';
    }
    draw_text(hdc, x, yy, buf, kTextDim, 12);
    yy += 14;

    char status[128] = {0};
    char* p = status;
    std::size_t n = sizeof(status);
    if (g_manual_pause) {
        int written = snprintf(p, n, "PAUSED");
        if (written > 0) {
            p += written;
            n -= written;
        }
    }
    if (g_sim_speed != 1.0) {
        int written = snprintf(p, n, "%sSpeed x%.2g",
                               (status[0] != '\0') ? " | " : "",
                               g_sim_speed);
        if (written > 0) {
            p += written;
            n -= written;
        }
    }
    if (g_bot_active) {
        snprintf(p, n, "%sBOT P2", (status[0] != '\0') ? " | " : "");
    }
    if (status[0] != '\0') {
        draw_text(hdc, x, yy, status, kTextMain, 12);
        yy += 14;
    }

    if (g_assets && !g_assets->fallback_status().empty()) {
        draw_text(hdc, x, yy, g_assets->fallback_status().c_str(), kTextDim, 12);
        yy += 14;
    }

    if (g_passive || g_use_monitor_placement) {
        char monitor[128] = {};
        if (g_passive && g_use_monitor_placement) {
            snprintf(monitor, sizeof(monitor), "PASSIVE | %s",
                     g_monitor_name_narrow.c_str());
        } else if (g_passive) {
            snprintf(monitor, sizeof(monitor), "PASSIVE");
        } else {
            snprintf(monitor, sizeof(monitor), "MONITOR: %s",
                     g_monitor_name_narrow.c_str());
        }
        draw_text(hdc, x, yy, monitor, kTextDim, 12);
        yy += 14;
    }

    draw_text(hdc, x, yy,
              "F1 hitbox / F2 anim / B bot / N step / [ ] speed",
              kTextDim, 12);
}

static void render_setup(HDC hdc, int w, int h) {
    RECT bg = {0, 0, w, h};
    draw_rect(hdc, bg, kPanelBackground, kPanelBackground);

    draw_text(hdc, w / 2 - 70, 20, "Pulse Court - Character Selection",
              kTextMain, 18);

    const char* hero_names[3] = {"Kite", "Vale", "Bastion"};
    Character hero_chars[3] = {Character::Kite, Character::Vale, Character::Bastion};
    SheetId hero_idle_sheets[3] = {SheetId::KiteIdle, SheetId::ValeIdle,
                                   SheetId::BastionIdle};

    int card_w = 160;
    int card_h = 220;
    int cols = 3;
    int total_width = cols * card_w + (cols - 1) * kCardSpacing;
    int start_x = (w - total_width) / 2;
    int start_y = 80;
    int row_gap = 40;

    for (int player = 0; player < 2; ++player) {
        const char* label = (player == 0) ? "Player 1" : "Player 2";
        int label_y = start_y + player * (card_h + row_gap) - 24;
        draw_text(hdc, start_x, label_y, label, kTextMain, 14);

        for (int i = 0; i < 3; ++i) {
            int cx = start_x + i * (card_w + kCardSpacing);
            int cy = start_y + player * (card_h + row_gap);
            RECT card = {cx, cy, cx + card_w, cy + card_h};

            COLORREF border = kPanelBorder;
            COLORREF fill = kPanelBackground;
            bool selected = (g_chars[player] == hero_chars[i]);
            bool active = (g_active_player == player);

            if (selected) {
                fill = (player == 0) ? RGB(30, 50, 90) : RGB(90, 40, 30);
                border = (player == 0) ? kPlayer1Color : kPlayer2Color;
            }
            if (active && selected) {
                border = RGB(255, 255, 0);
            }

            draw_rect(hdc, card, fill, border);

            int portrait_size = 80;
            int px = cx + card_w / 2;
            int py = cy + 80;
            if (!g_assets || !g_assets->draw_sprite(hdc, hero_idle_sheets[i], 8, px, py,
                                                    portrait_size, portrait_size,
                                                    0.0f, 1.0f)) {
                draw_circle(hdc, px, py, portrait_size / 2,
                            (i == 0) ? kPlayer1Color : (i == 1) ? kTextDim : kPlayer2Color,
                            RGB(240, 240, 240));
            }

            if (g_assets) {
                (void)g_assets->draw_sprite(hdc, SheetId::AbilityIcons, i,
                                            cx + card_w - 24, cy + 24, 24, 24,
                                            0.0f, 1.0f);
            }

            draw_text(hdc, cx + card_w / 2 - 15, cy + card_h - 28, hero_names[i],
                      kTextMain, 14);
        }
    }

    int btn_w = 160;
    int btn_h = 36;
    int btn_x = (w - btn_w) / 2;
    int btn_y = h - 90;
    RECT btn = {btn_x, btn_y, btn_x + btn_w, btn_y + btn_h};
    draw_rect(hdc, btn, kPlayer1Color, kTextMain);
    draw_text(hdc, btn_x + 32, btn_y + 10, "Start Match", kTextMain, 14);

    draw_text(hdc, w / 2 - 140, h - 34,
              "Tab switch player  Left/Right select  Enter start  Esc quit",
              kTextDim, 12);
}

static void handle_setup_input() {
    if (g_key_pressed[VK_TAB]) {
        g_active_player = 1 - g_active_player;
        g_key_pressed[VK_TAB] = false;
    }
    if (g_key_pressed[VK_LEFT] || g_key_pressed[VK_RIGHT]) {
        Character& c = g_chars[g_active_player];
        if (c == Character::Kite) {
            c = (g_key_pressed[VK_LEFT]) ? Character::Bastion : Character::Vale;
        } else if (c == Character::Vale) {
            c = (g_key_pressed[VK_LEFT]) ? Character::Kite : Character::Bastion;
        } else {
            c = (g_key_pressed[VK_LEFT]) ? Character::Vale : Character::Kite;
        }
        g_key_pressed[VK_LEFT] = false;
        g_key_pressed[VK_RIGHT] = false;
    }
    if (g_key_pressed[VK_RETURN]) {
        g_start_match = true;
        g_key_pressed[VK_RETURN] = false;
    }
    if (g_key_pressed[VK_ESCAPE]) {
        g_quit_requested = true;
        g_key_pressed[VK_ESCAPE] = false;
    }

    if (g_mouse_clicked) {
        int card_w = 160;
        int card_h = 220;
        int cols = 3;
        int total_width = cols * card_w + (cols - 1) * kCardSpacing;
        int start_x = -1;  // computed below
        int start_y = 80;
        int row_gap = 40;

        RECT rc;
        GetClientRect(g_hwnd, &rc);
        int w = rc.right;
        start_x = (w - total_width) / 2;

        Character hero_chars[3] = {Character::Kite, Character::Vale, Character::Bastion};

        for (int player = 0; player < 2; ++player) {
            for (int i = 0; i < 3; ++i) {
                int cx = start_x + i * (card_w + kCardSpacing);
                int cy = start_y + player * (card_h + row_gap);
                if (g_mouse_x >= cx && g_mouse_x < cx + card_w &&
                    g_mouse_y >= cy && g_mouse_y < cy + card_h) {
                    g_active_player = player;
                    g_chars[player] = hero_chars[i];
                }
            }
        }

        int btn_w = 160;
        int btn_h = 36;
        int btn_x = (w - btn_w) / 2;
        int btn_y = rc.bottom - 90;
        if (g_mouse_x >= btn_x && g_mouse_x < btn_x + btn_w &&
            g_mouse_y >= btn_y && g_mouse_y < btn_y + btn_h) {
            g_start_match = true;
        }

        g_mouse_clicked = false;
    }
}

static void gallery_next(int dir) {
    int count = static_cast<int>(SheetId::Count);
    if (count <= 0 || !g_assets) return;
    int idx = g_gallery_sheet_index;
    for (int i = 0; i < count; ++i) {
        idx = (idx + dir + count) % count;
        if (g_assets->has(static_cast<SheetId>(idx))) {
            g_gallery_sheet_index = idx;
            g_gallery_frame = 0;
            return;
        }
    }
}

static void handle_gallery_input() {
    if (g_key_pressed[VK_ESCAPE]) {
        g_quit_requested = true;
        g_key_pressed[VK_ESCAPE] = false;
    }
    if (g_key_pressed[VK_UP]) {
        gallery_next(1);
        g_key_pressed[VK_UP] = false;
    }
    if (g_key_pressed[VK_DOWN]) {
        gallery_next(-1);
        g_key_pressed[VK_DOWN] = false;
    }
    if (g_gallery_sheet_index >= 0 && g_gallery_sheet_index < static_cast<int>(SheetId::Count) &&
        g_assets) {
        const AssetManager::SheetInfo& s =
            g_assets->info(static_cast<SheetId>(g_gallery_sheet_index));
        if (g_key_pressed[VK_LEFT]) {
            g_gallery_frame = (g_gallery_frame - 1 + s.frames) % s.frames;
            g_key_pressed[VK_LEFT] = false;
        }
        if (g_key_pressed[VK_RIGHT]) {
            g_gallery_frame = (g_gallery_frame + 1) % s.frames;
            g_key_pressed[VK_RIGHT] = false;
        }
    }
    clear_edge_triggers();
}

static void render_gallery(HDC hdc, int w, int h) {
    if (g_gallery_sheet_index < 0 || !g_assets ||
        !g_assets->has(static_cast<SheetId>(g_gallery_sheet_index))) {
        SetTextAlign(hdc, TA_CENTER | TA_TOP);
        draw_text(hdc, w / 2, h / 2 - 30, "No assets loaded", kTextMain, 18);
        draw_text(hdc, w / 2, h / 2, "Esc quit", kTextDim, 12);
        SetTextAlign(hdc, TA_LEFT | TA_TOP);
        return;
    }

    SheetId sheet = static_cast<SheetId>(g_gallery_sheet_index);
    const AssetManager::SheetInfo& s = g_assets->info(sheet);
    std::string name = narrow_arg(s.filename);

    char header[256];
    snprintf(header, sizeof(header),
             "Sheet: %s | Grid %dx%d | Frame %d/%d",
             name.c_str(), s.columns, s.rows, g_gallery_frame, s.frames);

    SetTextAlign(hdc, TA_CENTER | TA_TOP);
    draw_text(hdc, w / 2, 20, header, kTextMain, 16);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);

    Gdiplus::Bitmap* bmp = g_assets->get(sheet);
    int dest_w = 320;
    int dest_h = 320;
    if (bmp) {
        int img_w = static_cast<int>(bmp->GetWidth());
        int img_h = static_cast<int>(bmp->GetHeight());
        double cell_w = static_cast<double>(img_w) / s.columns;
        double cell_h = static_cast<double>(img_h) / s.rows;
        double cell_max = std::max(cell_w, cell_h);
        if (cell_max > 0.0) {
            double scale = std::min(static_cast<double>(w), static_cast<double>(h)) * 0.5 / cell_max;
            dest_w = static_cast<int>(cell_w * scale);
            dest_h = static_cast<int>(cell_h * scale);
        }
    }

    int cx = w / 2;
    int cy = (h * 3) / 5;
    (void)g_assets->draw_sprite(hdc, sheet, g_gallery_frame, cx, cy, dest_w, dest_h,
                                0.0f, 1.0f);

    int left = cx - dest_w / 2;
    int top = cy - dest_h / 2;
    int right = cx + dest_w / 2;
    int bottom = cy + dest_h / 2;
    draw_line(hdc, left, top, right, top, RGB(255, 255, 255), 1);
    draw_line(hdc, right, top, right, bottom, RGB(255, 255, 255), 1);
    draw_line(hdc, right, bottom, left, bottom, RGB(255, 255, 255), 1);
    draw_line(hdc, left, bottom, left, top, RGB(255, 255, 255), 1);

    SetTextAlign(hdc, TA_CENTER | TA_TOP);
    draw_text(hdc, w / 2, h - 40,
              "Up/Down: sheet   Left/Right: frame   Esc: quit", kTextDim, 12);
    SetTextAlign(hdc, TA_LEFT | TA_TOP);
}

static void render_hitbox_overlay(HDC hdc, int court_x, int court_y, int court_width,
                                  int court_height) {
    if (!g_sim) return;
    const GameState& s = g_sim->state();

    for (std::size_t i = 0; i < s.players.size(); ++i) {
        const PlayerState& p = s.players[i];
        int px = court_x + world_to_screen_x(p.position.x, court_width);
        int py = court_y + world_to_screen_y(p.position.y, court_height);
        COLORREF color = (i == 0) ? kPlayer1Color : kPlayer2Color;

        int pr = world_radius_to_screen(kPlayerRadius, court_width);
        draw_hollow_circle(hdc, px, py, pr, color, PS_DOT, 1);

        int sr = world_radius_to_screen(kStrikeReach + kCoreRadius, court_width);
        draw_hollow_circle(hdc, px, py, sr, color, PS_DOT, 1);

        Vec2 end_pos = {p.position.x + p.velocity.x * 30,
                        p.position.y + p.velocity.y * 30};
        int ex = court_x + world_to_screen_x(end_pos.x, court_width);
        int ey = court_y + world_to_screen_y(end_pos.y, court_height);
        int dx = ex - px;
        int dy = ey - py;
        int len = static_cast<int>(std::sqrt(static_cast<double>(dx) * dx +
                                              static_cast<double>(dy) * dy));
        if (len > 80) {
            ex = px + dx * 80 / len;
            ey = py + dy * 80 / len;
        }
        if (len > 0) {
            draw_line(hdc, px, py, ex, ey, color, 2);
        }

        if (p.effect_kind == EffectKind::AnchorWell ||
            p.effect_kind == EffectKind::PulseGate) {
            int effect_radius = (p.effect_kind == EffectKind::AnchorWell)
                                    ? kAnchorRadius
                                    : kGateRadius;
            int erx = court_x + world_to_screen_x(p.effect_position.x, court_width);
            int ery = court_y + world_to_screen_y(p.effect_position.y, court_height);
            int er = world_radius_to_screen(effect_radius, court_width);
            draw_hollow_circle(hdc, erx, ery, er, color, PS_DOT, 1);
        }
    }

    int cx = court_x + world_to_screen_x(s.core.position.x, court_width);
    int cy = court_y + world_to_screen_y(s.core.position.y, court_height);
    int cr = world_radius_to_screen(kCoreRadius, court_width);
    draw_hollow_circle(hdc, cx, cy, cr, kCoreColor, PS_DOT, 1);
}

static void render_anim_debug(HDC hdc, int court_x, int court_y, int court_width,
                              int court_height) {
    if (!g_sim) return;
    const GameState& s = g_sim->state();

    for (std::size_t i = 0; i < s.players.size(); ++i) {
        const PlayerState& p = s.players[i];
        int px = court_x + world_to_screen_x(p.position.x, court_width);
        int py = court_y + world_to_screen_y(p.position.y, court_height);
        int pr = world_radius_to_screen(kPlayerRadius, court_width);

        auto debug = g_anim.body_debug(i);
        char buf[128];
        snprintf(buf, sizeof(buf), "%s f%d/%d t%d", debug.anim_name, debug.frame,
                 debug.frame_count, debug.elapsed);

        SetTextAlign(hdc, TA_CENTER | TA_TOP);
        draw_text(hdc, px, py - pr - 20, buf, kTextMain, 12);
        SetTextAlign(hdc, TA_LEFT | TA_TOP);
    }
}

static void render(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right;
    int h = rc.bottom;
    if (w <= 0 || h <= 0) return;

    HDC hdc_win = GetDC(hwnd);
    HDC hdc = CreateCompatibleDC(hdc_win);
    HBITMAP bmp = CreateCompatibleBitmap(hdc_win, w, h);
    HBITMAP old_bmp = static_cast<HBITMAP>(SelectObject(hdc, bmp));

    RECT bg = {0, 0, w, h};
    FillRect(hdc, &bg, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    if (g_setup_mode) {
        render_setup(hdc, w, h);
    } else if (g_gallery_mode) {
        render_gallery(hdc, w, h);
    } else {
        int sidebar_x = w - kSidebarWidth - kCourtPadding;
        int sidebar_y = kCourtPadding;
        int sidebar_w = kSidebarWidth;
        int sidebar_h = h - kCourtPadding - kFooterHeight;

        int court_x = kCourtPadding;
        int court_y = kCourtPadding;
        int available_w = sidebar_x - kCourtPadding - court_x;
        int available_h = h - kCourtPadding - kFooterHeight;

        int draw_w = available_w;
        int draw_h = static_cast<int>(draw_w * 22.0 / 38.0);
        if (draw_h > available_h) {
            draw_h = available_h;
            draw_w = static_cast<int>(draw_h * 38.0 / 22.0);
        }
        int draw_x = court_x + (available_w - draw_w) / 2;
        int draw_y = court_y + (available_h - draw_h) / 2;

        int fps = 0;
        double now = std::chrono::duration<double>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
        while (!g_frame_times.empty() && now - g_frame_times.front() > 1.0) {
            g_frame_times.pop_front();
        }
        g_frame_times.push_back(now);
        fps = static_cast<int>(g_frame_times.size());

        if (draw_w > 0 && draw_h > 0) {
            RECT court_rc = {draw_x, draw_y, draw_x + draw_w, draw_y + draw_h};
            if (!g_assets ||
                !g_assets->draw_image_cropped(hdc, SheetId::Arena, draw_x, draw_y,
                                              draw_w, draw_h)) {
                draw_rect(hdc, court_rc, kCourtBackground, kCourtBackground);
            }

            draw_court_lines(hdc, *g_assets, draw_x, draw_y, draw_w, draw_h);

            g_anim.draw_entities(hdc, *g_assets, draw_x, draw_y, draw_w, draw_h);

            if (g_show_hitboxes) {
                render_hitbox_overlay(hdc, draw_x, draw_y, draw_w, draw_h);
            }
            if (g_show_anim_debug) {
                render_anim_debug(hdc, draw_x, draw_y, draw_w, draw_h);
            }

            render_footer(hdc, draw_x, draw_y + draw_h + 4);
        }

        render_sidebar(hdc, sidebar_x, sidebar_y, sidebar_w, sidebar_h, fps);

        char fps_buf[64];
        snprintf(fps_buf, sizeof(fps_buf), "FPS: %d", fps);
        draw_text(hdc, w - 90, h - 20, fps_buf, kTextDim, 12);
    }

    BitBlt(hdc_win, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(hdc);
    ReleaseDC(hwnd, hdc_win);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_KEYDOWN:
            if (wparam < 256) {
                g_key_pressed[wparam] = !g_key_state[wparam];
                g_key_state[wparam] = true;
            }
            return 0;
        case WM_KEYUP:
            if (wparam < 256) g_key_state[wparam] = false;
            return 0;
        case WM_LBUTTONDOWN:
            g_mouse_clicked = true;
            g_mouse_x = LOWORD(lparam);
            g_mouse_y = HIWORD(lparam);
            return 0;
        case WM_MOUSEACTIVATE:
            if (g_passive) return MA_NOACTIVATE;
            break;
        case WM_SETFOCUS:
            g_has_focus = true;
            return 0;
        case WM_KILLFOCUS:
            g_has_focus = false;
            g_mouse_clicked = false;
            std::memset(g_key_state, 0, sizeof(g_key_state));
            std::memset(g_key_pressed, 0, sizeof(g_key_pressed));
            g_pending_buttons = {};
            g_input = {};
            return 0;
        case WM_GETMINMAXINFO:
            if (g_min_track_width > 0 && g_min_track_height > 0) {
                MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
                mmi->ptMinTrackSize.x = g_min_track_width;
                mmi->ptMinTrackSize.y = g_min_track_height;
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int viewer_main(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                 LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    // Parse command line using Windows argv-style parsing so quoted values are
    // preserved.
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        MessageBoxA(nullptr, "Command-line parsing failed", "Pulse Court",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    bool left_set = false;
    bool right_set = false;
    g_setup_mode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = narrow_arg(argv[i]);
        if (arg == "--setup") {
            g_setup_mode = true;
        } else if (arg == "--gallery") {
            g_gallery_mode = true;
        } else if (arg == "--passive") {
            g_passive = true;
        } else if (arg == "--left" && i + 1 < argc) {
            left_set = parse_character(narrow_arg(argv[++i]).c_str(),
                                       g_chars[0]);
        } else if (arg == "--right" && i + 1 < argc) {
            right_set = parse_character(narrow_arg(argv[++i]).c_str(),
                                       g_chars[1]);
        } else if (arg == "--monitor" && i + 1 < argc) {
            g_monitor_name = argv[++i];
            g_monitor_name_narrow = narrow_arg(g_monitor_name.c_str());
            g_use_monitor_placement = true;
        }
    }
    LocalFree(argv);

    if (g_gallery_mode) {
        g_setup_mode = false;
    } else if (!left_set || !right_set) {
        g_setup_mode = true;
    }

    // Resolve the requested monitor before creating the window so a failure is
    // closed before anything is displayed.
    MonitorPlacementResult placement_result;
    if (g_use_monitor_placement) {
        placement_result = find_monitor_placement(
            g_monitor_name, kClientWidth, kClientHeight, WS_OVERLAPPEDWINDOW,
            FALSE);
        if (!placement_result.success) {
            std::wstring msg = L"Monitor \"";
            msg += g_monitor_name;
            if (placement_result.ambiguous) {
                msg += L"\" matches multiple displays. ";
            } else {
                msg += L"\" not found. ";
            }
            msg += L"Available monitors:\n\n";
            for (const auto& probe : placement_result.available) {
                msg += L"  - ";
                msg += probe.friendly_name;
                if (!probe.device_name.empty() &&
                    probe.device_name != probe.friendly_name) {
                    msg += L" (";
                    msg += probe.device_name;
                    msg += L")";
                }
                msg += L"\n";
            }
            if (AttachConsole(ATTACH_PARENT_PROCESS)) {
                fwprintf(stderr, L"%s\n", msg.c_str());
            }
            MessageBoxW(nullptr, msg.c_str(), L"Pulse Court",
                        MB_OK | MB_ICONERROR);
            return 1;
        }
    }

    Gdiplus::GdiplusStartupInput gdiplus_input;
    ULONG_PTR gdiplus_token = 0;
    Gdiplus::Status gdiplus_status = Gdiplus::GdiplusStartup(
        &gdiplus_token, &gdiplus_input, nullptr);
    if (gdiplus_status != Gdiplus::Ok) {
        MessageBoxA(nullptr, "GDI+ initialization failed", "Pulse Court",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    std::unique_ptr<AssetManager> assets_local = std::make_unique<AssetManager>();
    g_assets = assets_local.get();
    std::filesystem::path assets_dir = find_assets_directory();
    g_assets->load(assets_dir);

    if (g_gallery_mode) {
        gallery_next(1);
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kClassName;

    std::filesystem::path icon_path = assets_dir / "icon.ico";
    HICON hIcon = nullptr;
    if (std::filesystem::exists(icon_path)) {
        hIcon = reinterpret_cast<HICON>(
            LoadImageW(nullptr, icon_path.c_str(), IMAGE_ICON, 0, 0,
                       LR_LOADFROMFILE | LR_DEFAULTSIZE));
    }
    wc.hIcon = hIcon;

    if (!RegisterClassW(&wc)) {
        MessageBoxA(nullptr, "Window registration failed", "Pulse Court",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    RECT min_wr = {0, 0, kMinClientWidth, kMinClientHeight};
    AdjustWindowRect(&min_wr, WS_OVERLAPPEDWINDOW, FALSE);
    g_min_track_width = min_wr.right - min_wr.left;
    g_min_track_height = min_wr.bottom - min_wr.top;

    int window_x = CW_USEDEFAULT;
    int window_y = CW_USEDEFAULT;
    int window_width = 0;
    int window_height = 0;
    if (g_use_monitor_placement) {
        const MonitorPlacement& placement = placement_result.placement;
        window_x = placement.x;
        window_y = placement.y;
        window_width = placement.width;
        window_height = placement.height;
    } else {
        RECT wr = {0, 0, kClientWidth, kClientHeight};
        AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        window_width = wr.right - wr.left;
        window_height = wr.bottom - wr.top;
    }

    DWORD ex_style = g_passive ? WS_EX_NOACTIVATE : 0;
    const wchar_t* window_title = g_gallery_mode ? L"Pulse Court - Gallery" : L"Pulse Court";
    g_hwnd = CreateWindowExW(
        ex_style, kClassName, window_title, WS_OVERLAPPEDWINDOW, window_x,
        window_y, window_width, window_height, nullptr, nullptr, hInstance,
        nullptr);
    if (!g_hwnd) {
        MessageBoxA(nullptr, "Window creation failed", "Pulse Court",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_hwnd, g_passive ? SW_SHOWNOACTIVATE : nCmdShow);
    UpdateWindow(g_hwnd);

    Simulation sim(g_chars);
    g_sim = &sim;
    g_anim.reset(g_chars);

    g_accumulated_time = 0.0;
    g_render_accumulated_time = 0.0;
    g_manual_pause = false;
    g_reset_requested = false;

    auto last_time = std::chrono::steady_clock::now();

    while (!g_quit_requested) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_quit_requested = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_quit_requested) break;

        auto now = std::chrono::steady_clock::now();
        double delta = std::chrono::duration<double>(now - last_time).count();
        last_time = now;

        if (delta > kMaxCatchUp) delta = kMaxCatchUp;

        if (g_gallery_mode) {
            handle_gallery_input();
            g_render_accumulated_time += delta;
            if (g_render_accumulated_time >= kRenderDt) {
                g_render_accumulated_time -= kRenderDt;
                render(g_hwnd);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (g_setup_mode) {
            handle_setup_input();
            if (g_start_match) {
                g_setup_mode = false;
                g_start_match = false;
                sim.reset(g_chars);
                g_anim.reset(g_chars);
                g_trace.clear();
                g_accumulated_time = 0.0;
                g_render_accumulated_time = 0.0;
                g_manual_pause = false;
                g_reset_requested = false;
                g_input = {};
                g_pending_buttons = {};
                g_step_requested = false;
                clear_edge_triggers();
                continue;
            }
            render(g_hwnd);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (g_reset_requested) {
            sim.reset(g_chars);
            g_anim.reset(g_chars);
            g_trace.clear();
            g_reset_requested = false;
            g_accumulated_time = 0.0;
            g_render_accumulated_time = 0.0;
            g_manual_pause = false;
            g_input = {};
            g_pending_buttons = {};
            g_step_requested = false;
            continue;
        }

        poll_input();

        bool should_step = (g_has_focus || g_passive) && !g_manual_pause;
        if (should_step) {
            g_accumulated_time += delta * g_sim_speed;
            while (g_accumulated_time >= kSimDt) {
                std::array<FrameInput, 2> in = g_input;
                in[0].buttons = g_pending_buttons[0];
                in[1].buttons = g_pending_buttons[1];
                g_pending_buttons = {};
                if (g_bot_active) {
                    in[1] = compute_bot_input(sim.state());
                }
                advance_tick(sim, in);
                g_accumulated_time -= kSimDt;
            }
        }

        if (g_step_requested) {
            std::array<FrameInput, 2> in = g_input;
            in[0].buttons = g_pending_buttons[0];
            in[1].buttons = g_pending_buttons[1];
            g_pending_buttons = {};
            if (g_bot_active) {
                in[1] = compute_bot_input(sim.state());
            }
            advance_tick(sim, in);
            g_step_requested = false;
        }

        g_render_accumulated_time += delta;
        if (g_render_accumulated_time >= kRenderDt) {
            g_render_accumulated_time -= kRenderDt;
            render(g_hwnd);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    DestroyWindow(g_hwnd);
    g_assets = nullptr;
    assets_local.reset();
    Gdiplus::GdiplusShutdown(gdiplus_token);
    return 0;
}

}  // namespace pulse

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow) {
    return pulse::viewer_main(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}
