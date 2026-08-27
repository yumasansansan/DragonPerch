// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"
#include "dragonperch/world.hpp"

#include <cstdint>
#include <vector>

namespace dp {

/// A window as a backend found it, before occlusion is taken into account.
///
/// This is the last point at which anything resembling a window exists. Every backend
/// discovers them differently -- `EnumWindows` plus DWM on Windows, a KWin script over
/// D-Bus on Plasma, EWMH on X11 -- and they all arrive here, as a rectangle and a place in
/// the stack.
struct WindowCandidate {
    std::int64_t id = 0;
    PixelRect frame{};

    /// Higher is nearer the front. Only the ordering matters.
    int z = 0;

    EdgeKind kind = EdgeKind::window_top;
};

/// Turns stacked window rectangles into walkable edges, clipping each top edge to the part
/// of it that is actually visible.
///
/// Not optional. Without it three maximised windows produce three identical full-width
/// ledges at the same y, and since the overlay always draws on top, a pet standing on a
/// buried ledge appears to float over the window covering it.
///
/// Only the longest visible run of each window is kept, so a window whose title bar is
/// interrupted in the middle contributes one segment rather than several. The core looks a
/// perch up by owner id and takes a single match, so several segments for one window would
/// make a walking pet teleport between them. One segment per window is the honest fit for
/// that model; splitting properly means giving edges their own identity, which is a change
/// to the core.
///
/// Sorts `candidates` in place. Edges narrower than `minimum_width` are dropped: those are
/// tooltip slivers and one-button toolboxes, not perches.
void append_window_edges(std::vector<WindowCandidate>& candidates, int minimum_width,
                         std::vector<WalkableEdge>& edges);

/// Longest sub-interval of `[left, right)` that none of `covers` overlaps.
///
/// Exposed for its own sake because it is the part that is easy to get subtly wrong and
/// hard to see going wrong on screen. Sorts `covers` in place.
[[nodiscard]] std::pair<int, int> longest_visible_run(int left, int right,
                                                      std::vector<std::pair<int, int>>& covers);

} // namespace dp
