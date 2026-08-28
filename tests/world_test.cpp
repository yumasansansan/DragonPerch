// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/world.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace dp;

namespace {

WalkableEdge edge(std::int64_t owner, int y, int left, int right, int z = 0)
{
    return WalkableEdge{owner, y, left, right, EdgeKind::window_top, z};
}

} // namespace

TEST_CASE("edges sort by y, then by z descending", "[world]")
{
    std::vector<WalkableEdge> edges{
        edge(1, 100, 0, 500, 5),
        edge(2, 50, 0, 500, 1),
        edge(3, 50, 0, 500, 9),
    };

    WorldSnapshot::sort(edges);

    // Same row: the one in front comes first, so a downward scan finds it before the one
    // buried behind it.
    CHECK(edges[0].owner_id == 3);
    CHECK(edges[1].owner_id == 2);
    CHECK(edges[2].owner_id == 1);
}

TEST_CASE("edge_below finds the highest edge under a point", "[world]")
{
    std::vector<WalkableEdge> edges{
        edge(1, 300, 0, 500),
        edge(2, 100, 0, 500),
        edge(3, 200, 600, 900), // elsewhere on the x axis
    };
    WorldSnapshot::sort(edges);
    const WorldSnapshot world{1, edges, {}};

    SECTION("picks the nearest one below, not merely the first in the list")
    {
        const WalkableEdge* found = world.edge_below(PixelPoint{250, 0});
        REQUIRE(found != nullptr);
        CHECK(found->owner_id == 2);
    }

    SECTION("ignores edges that do not span the x coordinate")
    {
        const WalkableEdge* found = world.edge_below(PixelPoint{700, 0});
        REQUIRE(found != nullptr);
        CHECK(found->owner_id == 3);
    }

    SECTION("strictly below: an edge at the same y is not under it")
    {
        CHECK(world.edge_below(PixelPoint{250, 300}) == nullptr);
    }
}

TEST_CASE("edges are half-open on the x axis", "[world]")
{
    const WalkableEdge e = edge(1, 0, 10, 20);

    CHECK(e.contains_x(10));
    CHECK(e.contains_x(19));
    CHECK_FALSE(e.contains_x(20));
    CHECK_FALSE(e.contains_x(9));
    CHECK(e.width() == 10);
}


TEST_CASE("a swept edge_below does not reach past where the fall ends", "[world]")
{
    // What a falling pet asks. At terminal velocity one frame covers more than a title
    // bar's height, so the question is "what did I pass through", not "what is under me".
    std::vector<WalkableEdge> edges{
        WalkableEdge{1, 100, 0, 400, EdgeKind::window_top, 0},
        WalkableEdge{2, 500, 0, 400, EdgeKind::window_top, 0},
    };
    WorldSnapshot::sort(edges);
    const WorldSnapshot world{1, std::move(edges), {}};

    // Falling from y=0 to y=200 passes the first and not the second.
    const WalkableEdge* hit = world.edge_below(PixelPoint{200, 0}, 200, 0);
    REQUIRE(hit != nullptr);
    CHECK(hit->owner_id == 1);

    // Stopping short of both lands on neither -- the bug this replaces was a point test at
    // the destination, which would have missed the edge at 100 entirely.
    CHECK(world.edge_below(PixelPoint{200, 0}, 50, 0) == nullptr);

    // Falling far enough for both still takes the higher.
    const WalkableEdge* far = world.edge_below(PixelPoint{200, 0}, 900, 0);
    REQUIRE(far != nullptr);
    CHECK(far->owner_id == 1);
}

TEST_CASE("a swept edge_below skips slivers", "[world]")
{
    // A ten-pixel ledge is the visible corner of something mostly hidden. Landing on it
    // looks like a bug, so the minimum width is asked for here rather than filtered after.
    std::vector<WalkableEdge> edges{
        WalkableEdge{1, 100, 200, 210, EdgeKind::window_top, 0},
        WalkableEdge{2, 300, 0, 400, EdgeKind::window_top, 0},
    };
    WorldSnapshot::sort(edges);
    const WorldSnapshot world{1, std::move(edges), {}};

    const WalkableEdge* hit = world.edge_below(PixelPoint{205, 0}, 900, 64);
    REQUIRE(hit != nullptr);
    CHECK(hit->owner_id == 2);

    // Without the minimum it is the sliver that answers, which is what makes this worth a
    // parameter rather than a caller's job.
    const WalkableEdge* any = world.edge_below(PixelPoint{205, 0}, 900, 0);
    REQUIRE(any != nullptr);
    CHECK(any->owner_id == 1);
}
