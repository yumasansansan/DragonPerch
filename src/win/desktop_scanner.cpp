// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_scanner.hpp"

#include "dragonperch/edge_builder.hpp"

#include "log.hpp"
#include "win_headers.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace dp::win::desktop_scanner {
namespace {

struct WindowScanState {
    std::vector<WindowCandidate> candidates;
    HWND shell = nullptr;
    int z = 0;
};

bool describe(HWND hwnd, int z, WindowCandidate& out)
{
    if (IsWindowVisible(hwnd) == FALSE || IsIconic(hwnd) != FALSE) {
        return false;
    }

    // Top-level only. Owned dialogs are fine to stand on, but child windows report
    // coordinates that are not directly comparable here.
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) {
        return false;
    }

    const auto ex_style = static_cast<LONG_PTR>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    if ((ex_style & WS_EX_TOOLWINDOW) != 0) {
        return false;
    }

    const auto style = static_cast<LONG_PTR>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    if ((style & WS_CHILD) != 0) {
        return false;
    }

    // The one that catches everybody. A cloaked window is invisible but reports a perfectly
    // plausible rectangle: suspended UWP apps and windows on other virtual desktops are
    // cloaked, so without this the desktop fills up with ledges hanging in mid-air.
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
        && cloaked != FALSE) {
        return false;
    }

    // DWMWA_EXTENDED_FRAME_BOUNDS, not GetWindowRect. GetWindowRect includes the invisible
    // resize border DWM keeps outside the visible frame -- roughly 7px per side on a
    // standard window, enough that a pet visibly floats off the left edge of the title
    // bar.
    RECT frame{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))
        && GetWindowRect(hwnd, &frame) == FALSE) {
        return false;
    }

    if (frame.right - frame.left < minimum_window_width
        || frame.bottom - frame.top < minimum_window_height) {
        return false;
    }

    out = WindowCandidate{
        .id = reinterpret_cast<std::int64_t>(hwnd),
        .frame = PixelRect::from_edges(frame.left, frame.top, frame.right, frame.bottom),
        .z = z,
        .kind = EdgeKind::window_top,
    };
    return true;
}

BOOL CALLBACK enum_window(HWND hwnd, LPARAM lparam)
{
    auto& state = *reinterpret_cast<WindowScanState*>(lparam);

    WindowCandidate candidate;
    if (hwnd != state.shell && describe(hwnd, state.z, candidate)) {
        state.candidates.push_back(candidate);
    }

    --state.z;
    return TRUE;
}

void scan_windows(std::vector<WalkableEdge>& edges)
{
    WindowScanState state;
    state.shell = GetShellWindow();

    // EnumWindows already walks top-to-bottom in Z order, so a descending counter is the Z
    // rank. A second GetWindow(GW_HWNDNEXT) pass would be redundant.
    state.z = std::numeric_limits<int>::max();

    EnumWindows(&enum_window, reinterpret_cast<LPARAM>(&state));

    // Full rectangles are needed before any edge can be emitted, because whether a title bar
    // is walkable depends on every window stacked above it. The clipping itself is pure
    // geometry and lives in the core, shared with the KWin provider.
    append_window_edges(state.candidates, minimum_window_width, edges);
}

BOOL CALLBACK enum_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM lparam)
{
    auto& outputs = *reinterpret_cast<std::vector<OutputInfo>*>(lparam);

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) == FALSE) {
        return TRUE;
    }

    // Per-monitor DPI. The manifest declares PerMonitorV2, so these rectangles are already
    // physical pixels; the scale is carried only so a renderer can pick a sprite size.
    double scale = 1.0;
    UINT dpi_x = 0;
    UINT dpi_y = 0;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)) && dpi_x > 0) {
        scale = static_cast<double>(dpi_x) / 96.0;
    }

    outputs.push_back(OutputInfo{
        .id = reinterpret_cast<std::int64_t>(monitor),
        .bounds = PixelRect::from_edges(info.rcMonitor.left, info.rcMonitor.top,
                                        info.rcMonitor.right, info.rcMonitor.bottom),
        .work_area = PixelRect::from_edges(info.rcWork.left, info.rcWork.top, info.rcWork.right,
                                           info.rcWork.bottom),
        .scale = scale,
        .name = to_utf8(info.szDevice),
    });

    return TRUE;
}

std::vector<OutputInfo> scan_outputs()
{
    std::vector<OutputInfo> outputs;
    EnumDisplayMonitors(nullptr, nullptr, &enum_monitor, reinterpret_cast<LPARAM>(&outputs));
    return outputs;
}

void add_taskbar(std::vector<WalkableEdge>& edges)
{
    // The taskbar does turn up in EnumWindows, but its reported rectangle is not something
    // to trust across auto-hide and multi-monitor setups. ABM_GETTASKBARPOS is the
    // supported query.
    APPBARDATA data{};
    data.cbSize = sizeof(data);

    if (SHAppBarMessage(ABM_GETTASKBARPOS, &data) == 0) {
        return;
    }

    const RECT& r = data.rc;
    if (r.right - r.left < minimum_window_width) {
        return;
    }

    // Only a horizontal taskbar has a top edge worth walking. A vertically docked one is a
    // wall, and walls are a separate feature.
    if (r.bottom - r.top > r.right - r.left) {
        return;
    }

    edges.push_back(WalkableEdge{
        .owner_id = taskbar_owner_id,
        .y = r.top,
        .left = r.left,
        .right = r.right,
        .kind = EdgeKind::panel_top,
        .z_order = std::numeric_limits<int>::min() + 1,
    });
}

void add_screen_floors(std::vector<WalkableEdge>& edges, const std::vector<OutputInfo>& outputs)
{
    for (const OutputInfo& output : outputs) {
        // Bottom of the work area, so a pet that runs out of windows lands above the
        // taskbar rather than behind it. When a taskbar is docked at the bottom that is
        // exactly the taskbar's own top edge, so skip it rather than stack two ledges on the
        // same pixel row.
        const int y = output.work_area.bottom();

        const bool covered_by_panel = std::ranges::any_of(edges, [&](const WalkableEdge& e) {
            return e.kind == EdgeKind::panel_top && e.y == y && e.left <= output.work_area.left()
                   && e.right >= output.work_area.right();
        });

        if (covered_by_panel) {
            continue;
        }

        edges.push_back(WalkableEdge{
            .owner_id = -output.id,
            .y = y,
            .left = output.work_area.left(),
            .right = output.work_area.right(),
            .kind = EdgeKind::screen_floor,
            .z_order = std::numeric_limits<int>::min(),
        });
    }
}

} // namespace

Scan scan()
{
    Scan result;
    result.outputs = scan_outputs();

    scan_windows(result.edges);
    add_taskbar(result.edges);
    add_screen_floors(result.edges, result.outputs);

    WorldSnapshot::sort(result.edges);
    return result;
}

} // namespace dp::win::desktop_scanner
