#define NOMINMAX

#include "viewer_monitor.hpp"

#include <cwctype>
#include <cstring>
#include <algorithm>

namespace pulse {
namespace {

std::wstring normalize_name(const std::wstring& s) {
    std::wstring r;
    r.reserve(s.size());
    for (wchar_t c : s) {
        if (std::iswalnum(static_cast<std::wint_t>(c))) {
            r += static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
        }
    }
    // Treat "scepter" as an alias for "sceptre".
    static const std::wstring kScepter = L"scepter";
    static const std::wstring kSceptre = L"sceptre";
    std::size_t pos = 0;
    while ((pos = r.find(kScepter, pos)) != std::wstring::npos) {
        r.replace(pos, kScepter.size(), kSceptre);
        pos += kSceptre.size();
    }
    return r;
}

struct ConfigNamePair {
    std::wstring gdi_device;
    std::wstring friendly_name;
};

std::vector<ConfigNamePair> enumerate_display_config_names() {
    std::vector<ConfigNamePair> result;

    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    LONG status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, nullptr,
                                     &mode_count, nullptr, nullptr);
    if (status != ERROR_SUCCESS && status != ERROR_INSUFFICIENT_BUFFER) {
        return result;
    }
    if (path_count == 0 || mode_count == 0) {
        return result;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(),
                                &mode_count, modes.data(), nullptr);
    if (status != ERROR_SUCCESS) {
        return result;
    }

    paths.resize(path_count);
    for (const auto& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) {
            continue;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) {
            continue;
        }

        result.push_back({source.viewGdiDeviceName, target.monitorFriendlyDeviceName});
    }

    return result;
}

struct EnumContext {
    std::vector<MonitorProbe> monitors;
};

BOOL CALLBACK monitor_enum_proc(HMONITOR hMonitor, HDC hdc, LPRECT, LPARAM data) {
    (void)hdc;
    auto* ctx = reinterpret_cast<EnumContext*>(data);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMonitor, &mi)) {
        MonitorProbe probe;
        probe.device_name = mi.szDevice;
        probe.work_area = mi.rcWork;
        ctx->monitors.push_back(probe);
    }
    return TRUE;
}

std::vector<MonitorProbe> enumerate_active_monitors() {
    std::vector<ConfigNamePair> names = enumerate_display_config_names();
    EnumContext ctx = {};
    EnumDisplayMonitors(nullptr, nullptr, monitor_enum_proc, reinterpret_cast<LPARAM>(&ctx));

    for (auto& probe : ctx.monitors) {
        for (const auto& name : names) {
            if (probe.device_name == name.gdi_device) {
                probe.friendly_name = name.friendly_name;
                break;
            }
        }
        if (probe.friendly_name.empty() ||
            probe.friendly_name == probe.device_name) {
            DISPLAY_DEVICEW dd = {};
            dd.cb = sizeof(dd);
            if (EnumDisplayDevicesW(probe.device_name.c_str(), 0, &dd, 0)) {
                probe.friendly_name = dd.DeviceString;
            }
        }
        if (probe.friendly_name.empty()) {
            probe.friendly_name = probe.device_name;
        }
    }

    return ctx.monitors;
}

}  // namespace

MonitorPlacementResult find_monitor_placement(const std::wstring& requested_name,
                                               int client_width,
                                               int client_height,
                                               DWORD style,
                                               BOOL menu) {
    MonitorPlacementResult result;
    result.available = enumerate_active_monitors();
    if (result.available.empty()) {
        return result;
    }

    std::wstring q = normalize_name(requested_name);
    if (q.empty()) {
        return result;
    }

    std::size_t match_index = result.available.size();
    std::size_t match_count = 0;
    for (std::size_t i = 0; i < result.available.size(); ++i) {
        const std::wstring n = normalize_name(result.available[i].friendly_name);
        if (n.find(q) != std::wstring::npos || q.find(n) != std::wstring::npos) {
            match_index = i;
            ++match_count;
        }
    }

    if (match_count != 1) {
        result.ambiguous = (match_count > 1);
        return result;
    }

    const MonitorProbe& probe = result.available[match_index];
    RECT rect = {0, 0, client_width, client_height};
    AdjustWindowRect(&rect, style, menu);
    int window_width = rect.right - rect.left;
    int window_height = rect.bottom - rect.top;

    int work_width = probe.work_area.right - probe.work_area.left;
    int work_height = probe.work_area.bottom - probe.work_area.top;

    MonitorPlacement placement;
    placement.client_width = client_width;
    placement.client_height = client_height;
    placement.width = window_width;
    placement.height = window_height;
    placement.x = probe.work_area.left + std::max(0, (work_width - window_width) / 2);
    placement.y = probe.work_area.top + std::max(0, (work_height - window_height) / 2);

    result.placement = placement;
    result.success = true;
    return result;
}

}  // namespace pulse
