// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace dp::win {

/// DragonPerch.Shell.exe, seen from the daemon.
///
/// The Fluent tray menu lives in a process of its own, for a reason §13.3 of docs/plan.md
/// measured rather than assumed: initialising XAML costs a process about 50 MB of private
/// bytes permanently, and closing it again returns none of it. So the toolkit goes
/// somewhere it can be started on demand and killed without the pets noticing.
///
/// Everything here is optional. The shell may not be installed, may not have started yet,
/// or may have been killed; in every case the daemon falls back to its own Win32 menu.
/// That is not a degraded mode to apologise for -- the daemon has to run on its own.
namespace shell {

/// Is a shell listening in this session?
[[nodiscard]] bool is_listening();

/// Asks the running shell to show its menu at a point in physical screen pixels.
/// False when there is nobody to ask, or the ask did not land.
[[nodiscard]] bool show_menu(int x, int y, bool paused);

/// Starts one, if it is installed and not already running.
///
/// Called when the pointer arrives over the tray icon, which buys the couple of hundred
/// milliseconds a person spends moving the mouse and pressing the button -- about what a
/// cold WinUI process needs to put a window on the screen. Cheap to call repeatedly: it
/// does nothing when a shell is already there, and stops trying when there is no
/// executable to start.
void start(int menu_x, int menu_y, bool paused);

} // namespace shell
} // namespace dp::win
