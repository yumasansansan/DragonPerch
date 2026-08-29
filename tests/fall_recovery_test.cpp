// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/placeholder_pack.hpp"
#include "dragonperch/simulation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

using namespace dp;

namespace {

WalkableEdge ledge(std::int64_t owner, int y, int left, int right, EdgeKind kind, int z)
{
    return WalkableEdge{owner, y, left, right, kind, z};
}

WorldSnapshot desktop(std::vector<WalkableEdge> edges)
{
    WorldSnapshot::sort(edges);

    std::vector<OutputInfo> outputs{OutputInfo{
        .id = 1,
        .bounds = PixelRect::from_edges(0, 0, 1920, 1080),
        .work_area = PixelRect::from_edges(0, 0, 1920, 1032),
        .scale = 1.0,
        .name = "DISPLAY1",
    }};

    return WorldSnapshot{1, std::move(edges), std::move(outputs)};
}

/// What `--dump-world` actually reports on a 1920x1080 Windows desktop with the taskbar
/// docked at the bottom and one maximised window: two ledges, both spanning the monitor
/// exactly, and no screen floor -- the scanner leaves that out when the taskbar already
/// covers the row.
WorldSnapshot windows_desktop()
{
    return desktop({
        ledge(1000, 0, 0, 1920, EdgeKind::window_top, 0),
        ledge(-1, 1032, 0, 1920, EdgeKind::panel_top, std::numeric_limits<int>::min() + 1),
    });
}

SimulationOptions brisk()
{
    SimulationOptions options;
    options.walk_speed = 500.0;      // reach the end of a ledge quickly
    options.turn_at_edge_chance = 0; // and always prefer stepping off, when that is allowed
    options.idle_interval = Duration::zero();
    return options;
}

} // namespace

TEST_CASE("a pet does not step off where nothing can catch it", "[simulation]")
{
    // The bug this is here for: every ledge on a Windows desktop ends on the same two
    // columns, so a pet stepping off one is over nothing whichever it left. It fell the
    // whole height of the screen, out of the world, and rained back down -- for ever, and
    // always down the same edge of the screen.
    const SpritePack pack = placeholder_pack::create(0);

    Simulation sim{brisk()};
    sim.set_world(windows_desktop());
    // Dropped just above the taskbar, so it lands on it and starts walking.
    sim.spawn(pack, PixelPoint{1900, 900});

    int lowest = std::numeric_limits<int>::min();
    for (int frame = 0; frame < 3600; ++frame) { // a minute at 60 Hz
        sim.update(Duration{1.0 / 60.0});
        lowest = std::max(lowest, sim.pets()[0].position().y);
    }

    // It paces the taskbar instead, turning at both ends.
    INFO("lowest y reached: " << lowest);
    CHECK(lowest <= 1032);
    CHECK(sim.pets()[0].perch().has_value());
}

TEST_CASE("a pet still steps off when there is somewhere to land", "[simulation]")
{
    // The other half of the same rule: looking before stepping must not turn every pet
    // into one that never leaves the ledge it started on.
    const SpritePack pack = placeholder_pack::create(0);

    Simulation sim{brisk()};
    sim.set_world(desktop({
        ledge(1000, 300, 700, 1000, EdgeKind::window_top, 5),
        ledge(-1, 1032, 0, 1920, EdgeKind::panel_top, std::numeric_limits<int>::min() + 1),
    }));

    // Dropped onto the small window, with the taskbar below it to land on.
    sim.spawn(pack, PixelPoint{960, 100});

    bool reached_the_taskbar = false;
    for (int frame = 0; frame < 3600 && !reached_the_taskbar; ++frame) {
        sim.update(Duration{1.0 / 60.0});
        reached_the_taskbar = sim.pets()[0].perch() == -1;
    }

    CHECK(reached_the_taskbar);
}

TEST_CASE("a pet below the world is put back at the top of an output", "[simulation]")
{
    // The safety net behind the rule above, and the reason a pet is never lost for good:
    // whatever puts one under the screen -- a window closing beneath it, a monitor being
    // unplugged -- it comes back somewhere on the desktop rather than falling for ever.
    const SpritePack pack = placeholder_pack::create(0);

    Simulation sim{brisk()};
    sim.set_world(windows_desktop());

    // Well below the bottom of the only monitor, and outside every ledge's x range.
    sim.spawn(pack, PixelPoint{1950, 4000});

    bool back = false;
    for (int frame = 0; frame < 600 && !back; ++frame) {
        sim.update(Duration{1.0 / 60.0});
        back = sim.pets()[0].perch().has_value();
    }

    REQUIRE(back);
    CHECK(sim.pets()[0].position().x >= 0);
    CHECK(sim.pets()[0].position().x < 1920);
}
