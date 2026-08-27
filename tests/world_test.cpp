// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/world.hpp"

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("output_at maps a point to its monitor", "[world]")
{
    const OutputInfo left{1, PixelRect{0, 0, 1920, 1080}, PixelRect{0, 0, 1920, 1032}, 1.0, "left"};
    const OutputInfo right{2, PixelRect{1920, 0, 2560, 1440}, PixelRect{1920, 0, 2560, 1392}, 1.5,
                           "right"};
    const WorldSnapshot world{1, {}, {left, right}};

    REQUIRE(world.output_at(PixelPoint{100, 100}) != nullptr);
    CHECK(world.output_at(PixelPoint{100, 100})->id == 1);

    REQUIRE(world.output_at(PixelPoint{2000, 100}) != nullptr);
    CHECK(world.output_at(PixelPoint{2000, 100})->id == 2);

    CHECK(world.output_at(PixelPoint{-1, 0}) == nullptr);
}
