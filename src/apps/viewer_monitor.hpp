#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace pulse {

// Monitor placement information for the viewer window.
struct MonitorPlacement {
    int x = 0;           // Screen x of the top-left window corner.
    int y = 0;           // Screen y of the top-left window corner.
    int width = 0;       // Window width including decorations.
    int height = 0;      // Window height including decorations.
    int client_width = 0;
    int client_height = 0;
};

// Information about one active monitor.
struct MonitorProbe {
    std::wstring friendly_name;
    std::wstring device_name;
    RECT work_area{};
};

// Result of matching a requested monitor name.
struct MonitorPlacementResult {
    bool success = false;
    bool ambiguous = false;
    MonitorPlacement placement;
    std::vector<MonitorProbe> available;
};

// Find a monitor by friendly name and compute a centered window placement.
// requested_name is matched case-insensitively after normalizing spacing and
// punctuation; "scepter" and "sceptre" are treated as the same name.
// client_width/height describe the desired client area; style/menu are used
// to compute the final decorated window size.
MonitorPlacementResult find_monitor_placement(const std::wstring& requested_name,
                                               int client_width,
                                               int client_height,
                                               DWORD style = WS_OVERLAPPEDWINDOW,
                                               BOOL menu = FALSE);

}  // namespace pulse
