// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string_view>

namespace dp::win {

/// What one DragonPerch can be asked to do by another.
///
/// The tray needs *quit* and *pause*; the settings program will need *reload*. One
/// mechanism for all three, so that `--stop` is a caller of it rather than a thing of its
/// own -- which is what the named event it replaces had become.
enum class Command {
    quit,
    pause,
    resume,
    toggle_pause,
    reload,
};

[[nodiscard]] std::string_view name_of(Command command) noexcept;

/// The receiving end: a message-only window that answers `WM_COPYDATA`.
///
/// `WM_COPYDATA` rather than a pipe or a socket, because it is the one Win32 IPC that
/// arrives as a *message*. The overlay windows are already pumped on this thread, so the
/// same loop delivers control requests with no server thread and no polling. The tray icon
/// will hang off this window too -- `Shell_NotifyIcon` needs one anyway.
///
/// One per process. Creating a second is a programming error rather than a runtime one.
class ControlServer {
public:
    using Handler = std::function<void(Command)>;

    ControlServer() = default;
    ~ControlServer();

    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;
    ControlServer(ControlServer&&) = delete;
    ControlServer& operator=(ControlServer&&) = delete;

    /// Creates the window. `handler` runs on the pumping thread, so it may touch anything
    /// that thread owns. Returns false if the window could not be created, which is not
    /// worth failing the program over -- the pets still walk, they just cannot be told
    /// anything.
    [[nodiscard]] bool start(Handler handler);

private:
    void* window_ = nullptr; // HWND, kept opaque so this header stays out of windows.h
    Handler handler_;
};

/// The sending end. False when no DragonPerch is running in this session.
[[nodiscard]] bool send_command(Command command);

} // namespace dp::win
