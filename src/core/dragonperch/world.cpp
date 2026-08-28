// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/world.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace dp {

WorldSnapshot::WorldSnapshot(std::uint64_t version, std::vector<WalkableEdge> edges,
                             std::vector<OutputInfo> outputs)
    : version_(version)
    , edges_(std::move(edges))
    , outputs_(std::move(outputs))
{
}

void WorldSnapshot::sort(std::vector<WalkableEdge>& edges)
{
    std::ranges::sort(edges, [](const WalkableEdge& a, const WalkableEdge& b) {
        if (a.y != b.y) {
            return a.y < b.y;
        }
        return a.z_order > b.z_order;
    });
}

const WalkableEdge* WorldSnapshot::edge_below(PixelPoint from, int no_lower_than,
                                              int minimum_width) const noexcept
{
    for (const WalkableEdge& e : edges_) {
        if (e.y <= from.y || e.y > no_lower_than) {
            continue;
        }
        if (!e.contains_x(from.x) || e.width() < minimum_width) {
            continue;
        }
        // Sorted by y ascending, so the first hit is already the highest one.
        return &e;
    }
    return nullptr;
}

const WalkableEdge* WorldSnapshot::edge_below(PixelPoint from) const noexcept
{
    return edge_below(from, std::numeric_limits<int>::max(), 0);
}

const WalkableEdge* WorldSnapshot::find_by_owner(std::int64_t owner_id) const noexcept
{
    auto it = std::ranges::find(edges_, owner_id, &WalkableEdge::owner_id);
    return it == edges_.end() ? nullptr : &*it;
}

} // namespace dp
