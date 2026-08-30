// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragonperch/world.hpp"
#include "kwin_geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

/// The KWin script's report: the only input here that comes from another program.
///
/// Everything else this fuzzes is a file somebody wrote. This is a string sent over the
/// session bus by JavaScript running inside the compositor, in a format that is ours and
/// therefore has nobody else's parser to have found the holes in it first. It is split on
/// single spaces into a fixed-capacity array and the fields turned into integers by hand.
///
/// Not caught, deliberately: apply() has no business throwing on a malformed report. A
/// report is one message among many and the pets have to survive a bad one -- the script
/// can be an old version, or a new one, or half-written.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view report(reinterpret_cast<const char*>(data), size);

    dp::wl::KWinGeometryProvider provider;

    // Two monitors, because the report's `s` lines are matched against these by connector
    // name and a provider with no outputs would never take that path at all.
    const std::vector<dp::OutputInfo> outputs{
        dp::OutputInfo{.id = 1,
                       .bounds = dp::PixelRect::from_edges(0, 0, 1920, 1080),
                       .work_area = dp::PixelRect::from_edges(0, 0, 1920, 1080),
                       .scale = 1.0,
                       .name = "DP-1"},
        dp::OutputInfo{.id = 2,
                       .bounds = dp::PixelRect::from_edges(1920, 0, 3840, 1080),
                       .work_area = dp::PixelRect::from_edges(1920, 0, 3840, 1080),
                       .scale = 1.0,
                       .name = "eDP-1"},
    };
    provider.set_outputs(outputs);

    provider.apply(report);

    // Read back, because a snapshot that cannot be used is as bad as one that was never
    // made: edge_below walks the edges assuming the sort order apply() promised.
    const dp::WorldSnapshot snapshot = provider.current();
    for (const dp::WalkableEdge& edge : snapshot.edges()) {
        // Every number here came out of the report, and Update is published
        // SD_BUS_VTABLE_UNPRIVILEGED -- the sender is whatever in the session called it, not
        // necessarily the script. The parser refuses coordinates that could not describe a
        // screen so that the core's `x + width` cannot overflow; this is that promise,
        // checked rather than trusted. The bound is generous: it is twice what the parser
        // allows, which is what a left edge plus a width can add up to.
        constexpr int limit = 4000000;
        if (edge.left < -limit || edge.right > limit || edge.right < edge.left
            || edge.y < -limit || edge.y > limit) {
            std::abort();
        }

        // Safe to subtract from only because of the check above.
        (void)snapshot.edge_below(dp::PixelPoint{edge.left, edge.y - 1});
    }

    return 0;
}
