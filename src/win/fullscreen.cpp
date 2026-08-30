// SPDX-License-Identifier: GPL-3.0-or-later
#include "fullscreen.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"
#include "win_headers.hpp"

#include <array>
#include <string>

namespace dp::win::fullscreen {
namespace {

/// The desktop itself is permanently "full screen" and is not an app.
bool is_shell_window(HWND hwnd)
{
    if (hwnd == GetShellWindow() || hwnd == GetDesktopWindow()) {
        return true;
    }

    std::array<wchar_t, 64> cls{};
    const int length = GetClassNameW(hwnd, cls.data(), static_cast<int>(cls.size()));
    const std::wstring_view name{cls.data(), static_cast<std::size_t>(length)};

    // Progman and WorkerW are the desktop; Shell_TrayWnd is the taskbar, which is a full
    // width but never full height, so it would not trip the geometry test anyway.
    return name == L"Progman" || name == L"WorkerW" || name == L"Shell_TrayWnd";
}

/// A window that never appears in the taskbar and is not somebody's application.
///
/// Flyouts, thumbnails and other shell furniture set this; a game or a video player cannot,
/// because a full-screen app with no taskbar button would be one nobody could get back to.
/// Narrowing rather than guessing at class names: this is true of the whole category.
bool is_tool_window(HWND hwnd)
{
    return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
}

bool belongs_to_us(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

bool window_fills(HWND hwnd, const RECT& monitor)
{
    RECT frame{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))
        && GetWindowRect(hwnd, &frame) == FALSE) {
        return false;
    }

    // Covers, rather than equals. A borderless window is often a pixel or two larger than
    // the monitor, and an exact comparison would miss every one of them.
    return frame.left <= monitor.left && frame.top <= monitor.top && frame.right >= monitor.right
           && frame.bottom >= monitor.bottom;
}

} // namespace

bool covers(const PixelRect& output_bounds)
{
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || is_shell_window(foreground) || is_tool_window(foreground)
        || belongs_to_us(foreground)) {
        return false;
    }

    const HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL);
    if (monitor == nullptr) {
        return false;
    }

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) == FALSE) {
        return false;
    }

    // Is the foreground window even on the monitor being asked about? The shell state below
    // is global, so without this check one full-screen game would hide the pets on every
    // monitor.
    const PixelRect foreground_monitor = PixelRect::from_edges(
        info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom);
    if (foreground_monitor != output_bounds) {
        return false;
    }

    if (window_fills(foreground, info.rcMonitor)) {
        return true;
    }

    QUERY_USER_NOTIFICATION_STATE state{};
    if (FAILED(SHQueryUserNotificationState(&state))) {
        return false;
    }

    // QUNS_BUSY is deliberately not in this list. The shell reports it for any topmost
    // borderless window covering a monitor -- which is what this app's own overlay was,
    // before it was inset by a pixel -- so treating it as "a game is running" would be a
    // way for the pets to hide themselves.
    return state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE;
}

std::string describe_cover(const PixelRect& output_bounds)
{
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return " (no foreground window)";
    }

    std::array<wchar_t, 64> cls{};
    const int class_length = GetClassNameW(foreground, cls.data(), static_cast<int>(cls.size()));

    std::array<wchar_t, 96> title{};
    const int title_length = GetWindowTextW(foreground, title.data(), static_cast<int>(title.size()));

    RECT frame{};
    if (FAILED(DwmGetWindowAttribute(foreground, DWMWA_EXTENDED_FRAME_BOUNDS, &frame,
                                     sizeof(frame)))) {
        GetWindowRect(foreground, &frame);
    }

    QUERY_USER_NOTIFICATION_STATE state{};
    if (FAILED(SHQueryUserNotificationState(&state))) {
        state = QUNS_NOT_PRESENT;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);

    return cat(": class '", to_utf8(std::wstring_view{cls.data(),
                                                      static_cast<std::size_t>(class_length)}),
               "' title '",
               to_utf8(std::wstring_view{title.data(), static_cast<std::size_t>(title_length)}),
               "' pid ", pid, " at (", frame.left, ",", frame.top, ")-(", frame.right, ",",
               frame.bottom, ") vs monitor (", output_bounds.left(), ",", output_bounds.top(),
               ")-(", output_bounds.right(), ",", output_bounds.bottom(),
               "), notification state ", static_cast<int>(state),
               is_tool_window(foreground) ? ", tool window" : "");
}

} // namespace dp::win::fullscreen
