// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>

namespace dp::wl {

/// Where the geometry script is installed, or empty if it is nowhere to be found.
///
/// Looked for beside the executable, in the user's data directory, in the system one, and
/// then up the source tree -- the same order and for the same reason as the artwork.
[[nodiscard]] std::filesystem::path find_kwin_script();

/// Asks KWin to load the geometry script again, right now.
///
/// This is how DragonPerch gets a picture of the desktop at startup instead of standing
/// its pets on the bottom of the screen until somebody happens to move a window.
///
/// It cannot be done from the script's side. A KWin script is a statement list that runs
/// once when the compositor loads it -- at login, long before this program exists -- and
/// after that it only speaks when a window changes. A plain JavaScript script has no timer
/// to introduce itself with either. So the client asks the compositor to re-run it, at a
/// moment when the client is already listening, using KWin's own scripting D-Bus API.
///
/// Returns false when KWin is not there, or the script is not installed. Neither is fatal:
/// the pets still walk, on the bottom of the screen, until the first window moves.
[[nodiscard]] bool reload_kwin_script(const std::filesystem::path& script);

} // namespace dp::wl
