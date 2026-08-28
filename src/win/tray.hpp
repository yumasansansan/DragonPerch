// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "control.hpp"

#include <functional>

namespace dp::win {

/// The notification-area icon, and the menu behind it.
///
/// Its own message-only window rather than the control server's. `Shell_NotifyIcon` needs
/// a window to send its callback to and nothing else, and one that answers only its own
/// messages is a good deal easier to read than one routing two unrelated protocols.
///
/// The menu here is a plain `TrackPopupMenuEx`. Windows 11 gives that rounded corners and
/// the system accent for free, and it is what a tray icon is expected to produce -- but it
/// is *not* Fluent, and §13.3 of docs/plan.md hands the menu to a WinUI shell process when
/// one is installed. This is the path for when one is not, which the daemon has to have:
/// it runs on its own and must stay usable on its own.
class TrayIcon {
public:
    using Handler = std::function<void(Command)>;
    using PausedQuery = std::function<bool()>;

    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;
    TrayIcon(TrayIcon&&) = delete;
    TrayIcon& operator=(TrayIcon&&) = delete;

    /// Adds the icon. False if the window or the icon could not be made, which is worth
    /// saying and not worth failing over -- the pets still walk, they just have no menu.
    [[nodiscard]] bool add(Handler handler, PausedQuery paused);

private:
    void* window_ = nullptr; // HWND, kept opaque so this header stays out of windows.h
    Handler handler_;
    PausedQuery paused_;
};

} // namespace dp::win
