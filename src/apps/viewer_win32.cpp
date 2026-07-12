#include "pulse_sim.hpp"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace pulse {

namespace {

static Simulation* g_sim = nullptr;
static std::array<Character, 2> g_chars = {Character::Kite, Character::Vale};
static bool g_reset_requested = false;
static bool g_quit_requested = false;

const wchar_t kClassName[] = L"PulseCourtViewer";
const int kClientWidth = 960;
const int kClientHeight = 540;

int world_to_screen_x(std::int32_t world_x, int width) {
    std::int64_t v = static_cast<std::int64_t>(world_x) * width / kFieldWidth;
    return static_cast<int>(v);
}

int world_to_screen_y(std::int32_t world_y, int height) {
    std::int64_t v = static_cast<std::int64_t>(world_y) * height / kFieldHeight;
    return height - static_cast<int>(v);
}

int world_radius_to_screen(std::int32_t radius, int width) {
    std::int64_t v = static_cast<std::int64_t>(radius) * width / kFieldWidth;
    return static_cast<int>(v);
}

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
                        int style = PS_SOLID) {
    HPEN pen = CreatePen(style, 2, stroke);
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

const char* phase_name(Phase p) {
    switch (p) {
        case Phase::Kickoff: return "Kickoff";
        case Phase::Live: return "Live";
        case Phase::MatchOver: return "MatchOver";
    }
    return "?";
}

bool is_key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

FrameInput poll_left_input() {
    FrameInput in;
    int mx = 0;
    int my = 0;
    if (is_key(0x41)) mx -= 1;  // A
    if (is_key(0x44)) mx += 1;  // D
    if (is_key(0x57)) my += 1;  // W
    if (is_key(0x53)) my -= 1;  // S
    in.move_x = static_cast<std::int8_t>(mx);
    in.move_y = static_cast<std::int8_t>(my);
    in.aim_x = in.move_x;
    in.aim_y = in.move_y;
    if (is_key(0x46)) in.buttons |= ButtonStrike;   // F
    if (is_key(0x47)) in.buttons |= ButtonAbility;  // G
    if (is_key(0x48)) in.buttons |= ButtonDash;     // H
    return in;
}

FrameInput poll_right_input() {
    FrameInput in;
    int mx = 0;
    int my = 0;
    if (is_key(VK_LEFT)) mx -= 1;
    if (is_key(VK_RIGHT)) mx += 1;
    if (is_key(VK_UP)) my += 1;
    if (is_key(VK_DOWN)) my -= 1;
    in.move_x = static_cast<std::int8_t>(mx);
    in.move_y = static_cast<std::int8_t>(my);
    in.aim_x = in.move_x;
    in.aim_y = in.move_y;
    if (is_key(VK_NUMPAD1)) in.buttons |= ButtonStrike;
    if (is_key(VK_NUMPAD2)) in.buttons |= ButtonAbility;
    if (is_key(VK_NUMPAD3)) in.buttons |= ButtonDash;
    return in;
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
    SelectObject(hdc, bmp);

    // Background
    RECT bg = {0, 0, w, h};
    FillRect(hdc, &bg, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    // Field fill
    HBRUSH field_brush = CreateSolidBrush(RGB(20, 40, 20));
    RECT field_rc = {0, 0, w, h};
    FillRect(hdc, &field_rc, field_brush);
    DeleteObject(field_brush);

    // Center line
    int cx = w / 2;
    draw_line(hdc, cx, 0, cx, h, RGB(60, 80, 60), 2);
    draw_line(hdc, 0, h / 2, w, h / 2, RGB(60, 80, 60), 1);

    // Goals
    int goal_top = world_to_screen_y(kGoalBottom, h);
    int goal_bottom = world_to_screen_y(kGoalTop, h);
    HBRUSH goal_brush = CreateSolidBrush(RGB(40, 20, 20));
    RECT left_goal = {0, goal_top, cx, goal_bottom};
    RECT right_goal = {cx, goal_top, w, goal_bottom};
    FillRect(hdc, &left_goal, goal_brush);
    FillRect(hdc, &right_goal, goal_brush);
    DeleteObject(goal_brush);

    // Side/top/bottom walls (white), with goal mouth openings
    int r_screen = world_radius_to_screen(kCoreRadius, w);
    int wall_top = world_to_screen_y(kFieldHeight, h) - r_screen;
    int wall_bottom = world_to_screen_y(0, h) + r_screen;
    // Top and bottom across
    draw_line(hdc, 0, wall_top, w, wall_top, RGB(220, 220, 220), 3);
    draw_line(hdc, 0, wall_bottom, w, wall_bottom, RGB(220, 220, 220), 3);
    // Left side above and below goal
    int left_x = world_to_screen_x(0, w) + r_screen;
    draw_line(hdc, left_x, 0, left_x, goal_top, RGB(220, 220, 220), 3);
    draw_line(hdc, left_x, goal_bottom, left_x, h, RGB(220, 220, 220), 3);
    // Right side above and below goal
    int right_x = world_to_screen_x(kFieldWidth, w) - r_screen;
    draw_line(hdc, right_x, 0, right_x, goal_top, RGB(220, 220, 220), 3);
    draw_line(hdc, right_x, goal_bottom, right_x, h, RGB(220, 220, 220), 3);

    const GameState& s = g_sim->state();

    // Draw active effects behind entities
    for (const auto& p : s.players) {
        if (p.effect_ticks == 0) continue;
        if (p.effect_kind == EffectKind::AnchorWell) {
            int ex = world_to_screen_x(p.effect_position.x, w);
            int ey = world_to_screen_y(p.effect_position.y, h);
            int er = world_radius_to_screen(kAnchorRadius, w);
            draw_hollow_circle(hdc, ex, ey, er, RGB(0, 200, 200), PS_DOT);
        } else if (p.effect_kind == EffectKind::PulseGate) {
            int ex = world_to_screen_x(p.effect_position.x, w);
            int ey = world_to_screen_y(p.effect_position.y, h);
            int er = world_radius_to_screen(kGateRadius, w);
            draw_hollow_circle(hdc, ex, ey, er, RGB(255, 180, 0), PS_SOLID);
        } else if (p.effect_kind == EffectKind::Jetstep) {
            int px = world_to_screen_x(p.position.x, w);
            int py = world_to_screen_y(p.position.y, h);
            int pr = world_radius_to_screen(kPlayerRadius, w) + 8;
            draw_hollow_circle(hdc, px, py, pr, RGB(0, 200, 255), PS_DOT);
        }
    }

    // Core
    int core_x = world_to_screen_x(s.core.position.x, w);
    int core_y = world_to_screen_y(s.core.position.y, h);
    int core_r = world_radius_to_screen(kCoreRadius, w);
    draw_circle(hdc, core_x, core_y, core_r, RGB(240, 240, 240),
                RGB(200, 200, 200));

    // Players
    for (std::size_t i = 0; i < s.players.size(); ++i) {
        const auto& p = s.players[i];
        int px = world_to_screen_x(p.position.x, w);
        int py = world_to_screen_y(p.position.y, h);
        int pr = world_radius_to_screen(kPlayerRadius, w);
        COLORREF fill = (i == 0) ? RGB(60, 100, 220) : RGB(220, 80, 60);
        draw_circle(hdc, px, py, pr, fill, RGB(240, 240, 240));

        // Facing indicator
        int fx = px + (p.facing.x * pr) / kFixedScale;
        int fy = py - (p.facing.y * pr) / kFixedScale;
        draw_line(hdc, px, py, fx, fy, RGB(255, 255, 0), 2);
    }

    // HUD text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    HFONT font = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, L"Consolas");
    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font));

    char line[256];
    int y = 4;
    snprintf(line, sizeof(line), "PULSE COURT  |  %d - %d  |  %s  |  tick %u",
             s.score[0], s.score[1], phase_name(s.phase), s.tick);
    TextOutA(hdc, 10, y, line, static_cast<int>(strlen(line)));
    y += 22;

    snprintf(line, sizeof(line), "L=%s  R=%s  |  hash=%llu",
             character_name(s.players[0].character),
             character_name(s.players[1].character),
             static_cast<unsigned long long>(g_sim->state_hash()));
    TextOutA(hdc, 10, y, line, static_cast<int>(strlen(line)));
    y += 22;

    for (std::size_t i = 0; i < s.players.size(); ++i) {
        const auto& p = s.players[i];
        const char* side = (i == 0) ? "L" : "R";
        snprintf(line, sizeof(line),
                 "%s cd: strike=%d dash=%d ability=%d  effect=%d", side,
                 p.strike_cooldown, p.dash_cooldown, p.ability_cooldown,
                 p.effect_ticks);
        TextOutA(hdc, 10, y, line, static_cast<int>(strlen(line)));
        y += 22;
    }

    snprintf(line, sizeof(line),
             "Controls: L=WASD+F/G/H  R=Arrows+Num1/2/3  R=reset  Esc=quit");
    TextOutA(hdc, 10, h - 24, line, static_cast<int>(strlen(line)));

    SelectObject(hdc, old_font);
    DeleteObject(font);

    BitBlt(hdc_win, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    DeleteObject(bmp);
    DeleteDC(hdc);
    ReleaseDC(hwnd, hdc_win);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                         LPARAM lparam) {
    (void)lparam;
    switch (msg) {
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                g_quit_requested = true;
                return 0;
            }
            if (wparam == 'R') {
                g_reset_requested = true;
                return 0;
            }
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

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine,
                     int nCmdShow) {
    using namespace pulse;

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
        kClassName, L"Pulse Court",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, CW_USEDEFAULT,
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
            g_reset_requested = false;
        }

        if (GetForegroundWindow() == hwnd) {
            std::array<FrameInput, 2> inputs = {poll_left_input(),
                                                poll_right_input()};
            (void)sim.step(inputs);
        } else {
            // Window not focused: still step with neutral inputs so the
            // simulation does not freeze, but ignore keyboard.
            std::array<FrameInput, 2> inputs = {};
            (void)sim.step(inputs);
        }

        render(hwnd);
        Sleep(8);  // ~120 Hz fixed simulation tick cadence
    }

    DestroyWindow(hwnd);
    return 0;
}
