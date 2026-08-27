// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_scanner.hpp"

#include "log.hpp"
#include "win_headers.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace dp::win::desktop_scanner {
namespace {

/// A candidate window, before occlusion is taken into account.
struct Candidate {
    std::int64_t id = 0;
    RECT frame{};
    int z = 0;
};

struct WindowScanState {
    std::vector<Candidate> candidates;
    HWND shell = nullptr;
    int z = 0;
};

/// Longest sub-interval of [left, right) not covered by `covers`.
///
/// `covers` is sorted in place; the caller does not need it afterwards.
std::pair<int, int> longest_visible_run(int left, int right, std::vector<std::pair<int, int>>& covers)
{
    if (covers.empty()) {
        return {left, right};
    }

    std::ranges::sort(covers, {}, &std::pair<int, int>::first);

    int best_left = left;
    int best_right = left;
    int cursor = left;

    for (const auto& [cover_left, cover_right] : covers) {
        if (cover_left > cursor && cover_left - cursor > best_right - best_left) {
            best_left = cursor;
            best_right = cover_left;
        }

        cursor = std::max(cursor, cover_right);
        if (cursor >= right) {
            return {best_left, best_right};
        }
    }

    if (right - cursor > best_right - best_left) {
        best_left = cursor;
        best_right = right;
    }

    return {best_left, best_right};
}

/// Clips each window's top edge to the part of it that is actually visible.
///
/// Not optional. Without it three maximised windows produce three identical full-width
/// ledges at y=0, and since the overlay always draws on top, a pet standing on a buried
/// ledge appears to float over the window covering it.
///
/// Only the longest visible run is kept, so a window whose title bar is interrupted in the
/// middle contributes one segment rather than several. The core looks a perch up by owner
/// id and takes a single match, so several segments for one window would make a walking pet
/// teleport between them. One segment per window is the honest fit for that model;
/// splitting properly means giving edges their own identity, which is a change to the core.
void emit_window_edges(std::vector<Candidate>& candidates, std::vector<WalkableEdge>& edges)
{
    // Topmost first, so everything before index i is a potential occluder of i.
    std::ranges::sort(candidates, std::ranges::greater{}, &Candidate::z);

    std::vector<std::pair<int, int>> occluders;

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const Candidate& candidate = candidates[i];
        const int scanline = candidate.frame.top;

        occluders.clear();
        for (std::size_t j = 0; j < i; ++j) {
            const RECT& above = candidates[j].frame;

            // Does the window above cover the row this title bar occupies?
            if (above.top > scanline || above.bottom <= scanline) {
                continue;
            }

            const int cover_left = std::max(above.left, candidate.frame.left);
            const int cover_right = std::min(above.right, candidate.frame.right);
            if (cover_left < cover_right) {
                occluders.emplace_back(cover_left, cover_right);
            }
        }

        const auto [visible_left, visible_right] =
            longest_visible_run(candidate.frame.left, candidate.frame.right, occluders);

        if (visible_right - visible_left < minimum_window_width) {
            continue;
        }

        edges.push_back(WalkableEdge{
            .owner_id = candidate.id,
            .y = scanline,
            .left = visible_left,
            .right = visible_right,
            .kind = EdgeKind::window_top,
            .z_order = candidate.z,
        });
    }
}

bool describe(HWND hwnd, int z, Candidate& out)
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
    // standard window, enough that a dragon visibly floats off the left edge of the title
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

    out = Candidate{reinterpret_cast<std::int64_t>(hwnd), frame, z};
    return true;
}

BOOL CALLBACK enum_window(HWND hwnd, LPARAM lparam)
{
    auto& state = *reinterpret_cast<WindowScanState*>(lparam);

    Candidate candidate;
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
    // is walkable depends on every window stacked above it.
    emit_window_edges(state.candidates, edges);
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
        // Bottom of the work area, so a dragon that runs out of windows lands above the
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
