// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"

#include <string>

namespace dp::win::fullscreen {

/// True when a full-screen application is covering the monitor whose bounds these are.
///
/// A dragon walking across somebody's game or film is the fastest way to get an app
/// uninstalled, so this is checked every frame and the overlay on that monitor is hidden
/// outright rather than merely skipped -- a hidden window is not composited at all.
///
/// Two signals, because neither is sufficient alone:
///
///  - The foreground window's extended frame bounds against the monitor's. This catches
///    borderless-fullscreen, which is what most games and every video player use now.
///  - `SHQueryUserNotificationState`, which reports exclusive-fullscreen Direct3D and
///    presentation mode. An exclusive-fullscreen app may not report window bounds that
///    look like anything in particular, so the geometry test can miss it.
///
/// The shell state is global rather than per-monitor, so it is only applied to the monitor
/// the foreground window is actually on.
[[nodiscard]] bool covers(const PixelRect& output_bounds);

/// What covers() is looking at, for the log line that reports a monitor going dark.
///
/// Hiding the pets is the one thing this program does that a person notices and cannot
/// explain, and "hiding for a full-screen app" on its own does not say which app -- which is
/// no use at all when the app turns out to be a shell flyout that is not full screen and not
/// an app. Called only when the answer changes, so its cost does not matter.
[[nodiscard]] std::string describe_cover(const PixelRect& output_bounds);

} // namespace dp::win::fullscreen
