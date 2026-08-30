// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace dp::win {

/// DragonPerch.Shell.exe, seen from the daemon.
///
/// The Fluent tray menu lives in a process of its own, for a reason §13.3 of docs/plan.md
/// measured rather than assumed: initialising XAML costs a process about 40 MB of private
/// bytes permanently, and closing it again returns none of it. So the toolkit goes
/// somewhere it can be started on demand and killed without the pets noticing.
///
/// Everything here is optional. The shell may not be installed, may not have started yet,
/// or may have been killed; in every case the daemon falls back to its own Win32 menu.
/// That is not a degraded mode to apologise for -- the daemon has to run on its own.
namespace shell {

/// Is a shell listening in this session?
[[nodiscard]] bool is_listening();

/// Is one installed beside the daemon, whether or not it is running?
///
/// What the daemon's own menu needs in order to decide whether its Settings item can do
/// anything -- the settings window lives in the shell, so an item that opens nothing is
/// worse than one that is visibly not available. The Linux tray greys its item on the same
/// question, asked of `kcmshell6`.
[[nodiscard]] bool is_installed();

/// Opens the settings window, starting a shell for it.
///
/// Only reached from the menu the daemon drew: when the shell draws the menu it opens the
/// window itself. False when there is nothing to start.
bool open_settings();

/// Asks the running shell to show its menu at a point in physical screen pixels.
/// False when there is nobody to ask, or the ask did not land.
[[nodiscard]] bool show_menu(int x, int y, bool paused);

/// Starts one, quietly, if it is installed and not already running.
///
/// Called when the pointer arrives over the tray icon, which buys the couple of hundred
/// milliseconds a person spends moving the mouse and pressing the button -- about what a
/// cold WinUI process needs to put a window on the screen. Cheap to call repeatedly: it
/// does nothing when a shell is already there, and stops trying when there is no
/// executable to start.
///
/// Deliberately does *not* show a menu. Hovering over a tray icon is not a request for
/// one, and a menu that opens because the pointer passed over the icon is worse than one
/// that takes an extra moment to appear.
void prewarm();

} // namespace shell
} // namespace dp::win
