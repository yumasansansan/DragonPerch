// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/edge_builder.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace dp {

std::pair<int, int> longest_visible_run(int left, int right,
                                        std::vector<std::pair<int, int>>& covers)
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

void append_window_edges(std::vector<WindowCandidate>& candidates, int minimum_width,
                         std::vector<WalkableEdge>& edges)
{
    // Topmost first, so everything before index i is a potential occluder of i.
    std::ranges::sort(candidates, std::ranges::greater{}, &WindowCandidate::z);

    std::vector<std::pair<int, int>> occluders;

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const WindowCandidate& candidate = candidates[i];
        const int scanline = candidate.frame.top();

        occluders.clear();
        for (std::size_t j = 0; j < i; ++j) {
            const PixelRect& above = candidates[j].frame;

            // Does the window above cover the row this title bar occupies?
            if (above.top() > scanline || above.bottom() <= scanline) {
                continue;
            }

            const int cover_left = std::max(above.left(), candidate.frame.left());
            const int cover_right = std::min(above.right(), candidate.frame.right());
            if (cover_left < cover_right) {
                occluders.emplace_back(cover_left, cover_right);
            }
        }

        const auto [visible_left, visible_right] =
            longest_visible_run(candidate.frame.left(), candidate.frame.right(), occluders);

        if (visible_right - visible_left < minimum_width) {
            continue;
        }

        edges.push_back(WalkableEdge{
            .owner_id = candidate.id,
            .y = scanline,
            .left = visible_left,
            .right = visible_right,
            .kind = candidate.kind,
            .z_order = candidate.z,
        });
    }
}

} // namespace dp
