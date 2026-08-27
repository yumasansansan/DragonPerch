// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/world.hpp"

#include <cstdint>
#include <vector>

namespace dp::win {

/// Turns the current Win32 desktop into walkable edges and outputs.
///
/// Pure query: no state, no hooks. `WinEventWatcher` decides when to call it.
namespace desktop_scanner {

struct Scan {
    std::vector<WalkableEdge> edges;
    std::vector<OutputInfo> outputs;
};

/// Sentinel owner id for the taskbar. Real ids are HWND or HMONITOR values, so a small
/// negative constant cannot collide with one.
inline constexpr std::int64_t taskbar_owner_id = -1;

/// Windows narrower or shorter than this are not perches, and are usually not application
/// windows at all.
inline constexpr int minimum_window_width = 96;
inline constexpr int minimum_window_height = 48;

[[nodiscard]] Scan scan();

} // namespace desktop_scanner
} // namespace dp::win
