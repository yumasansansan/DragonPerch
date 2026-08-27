// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/edge_builder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

using namespace dp;

namespace {

WindowCandidate window(std::int64_t id, int x, int y, int width, int height, int z)
{
    return WindowCandidate{id, PixelRect{x, y, width, height}, z, EdgeKind::window_top};
}

std::pair<int, int> run(int left, int right, std::vector<std::pair<int, int>> covers)
{
    return longest_visible_run(left, right, covers);
}

} // namespace

TEST_CASE("an uncovered interval is visible end to end", "[edges]")
{
    CHECK(run(0, 100, {}) == std::pair{0, 100});
}

TEST_CASE("a cover in the middle leaves the longer of the two sides", "[edges]")
{
    // 0..30 and 70..100 both survive the cover; the longer wins, and here they are equal
    // in length so the first one found is kept. What matters is that one whole run comes
    // back rather than a piece of each.
    CHECK(run(0, 100, {{30, 70}}) == std::pair{0, 30});
    CHECK(run(0, 100, {{20, 70}}) == std::pair{70, 100});
}

TEST_CASE("overlapping covers merge rather than splitting the gap", "[edges]")
{
    // Two occluders that overlap each other must not leave a phantom sliver between them.
    CHECK(run(0, 100, {{10, 50}, {40, 60}}) == std::pair{60, 100});
}

TEST_CASE("covers arrive in any order", "[edges]")
{
    // Windows come in stacking order, which has nothing to do with their x positions.
    CHECK(run(0, 100, {{80, 100}, {0, 20}}) == std::pair{20, 80});
}

TEST_CASE("a fully covered interval is empty", "[edges]")
{
    const auto [left, right] = run(0, 100, {{-10, 110}});
    CHECK(right - left == 0);
}

TEST_CASE("a covered title bar contributes only its visible part", "[edges]")
{
    // The case that made this necessary: two maximised windows produce two identical
    // full-width ledges at the same y, and because the overlay always draws on top, a pet
    // on the buried one appears to float over the window covering it.
    std::vector<WindowCandidate> candidates{
        window(1, 0, 0, 1920, 1080, 10),   // on top, covers everything
        window(2, 0, 0, 1920, 1080, 5),    // buried
    };

    std::vector<WalkableEdge> edges;
    append_window_edges(candidates, 64, edges);

    REQUIRE(edges.size() == 1);
    CHECK(edges[0].owner_id == 1);
}

TEST_CASE("a window above but not across leaves the title bar alone", "[edges]")
{
    // The window in front sits lower down the screen, so it does not cross the row the
    // title bar occupies and must not clip it.
    std::vector<WindowCandidate> candidates{
        window(1, 0, 400, 800, 300, 10),
        window(2, 0, 100, 800, 200, 5),
    };

    std::vector<WalkableEdge> edges;
    append_window_edges(candidates, 64, edges);

    REQUIRE(edges.size() == 2);
    const WalkableEdge& buried = edges[0].owner_id == 2 ? edges[0] : edges[1];
    CHECK(buried.left == 0);
    CHECK(buried.right == 800);
    CHECK(buried.y == 100);
}

TEST_CASE("a window in front takes a bite out of the one behind", "[edges]")
{
    std::vector<WindowCandidate> candidates{
        window(1, 300, 0, 800, 600, 10),
        window(2, 0, 100, 1000, 600, 5),
    };

    std::vector<WalkableEdge> edges;
    append_window_edges(candidates, 64, edges);

    REQUIRE(edges.size() == 2);
    const WalkableEdge& behind = edges[0].owner_id == 2 ? edges[0] : edges[1];

    // 0..300 is clear, 300..1000 is covered. The clear part is what is left.
    CHECK(behind.left == 0);
    CHECK(behind.right == 300);
}

TEST_CASE("slivers are not perches", "[edges]")
{
    // A window with ten visible pixels of title bar is a tooltip or the edge of something
    // mostly hidden. Standing a dragon on it looks like a bug.
    std::vector<WindowCandidate> candidates{
        window(1, 20, 0, 1900, 600, 10),
        window(2, 0, 0, 1000, 600, 5),
    };

    std::vector<WalkableEdge> edges;
    append_window_edges(candidates, 64, edges);

    REQUIRE(edges.size() == 1);
    CHECK(edges[0].owner_id == 1);
}

TEST_CASE("the edge kind is carried through", "[edges]")
{
    // Panels come through the same clipping as windows, and have to stay panels: the
    // simulation treats a panel as somewhere a pet may sit rather than a window that might
    // be dragged away.
    std::vector<WindowCandidate> candidates{
        WindowCandidate{7, PixelRect{0, 1000, 1920, 40}, 1, EdgeKind::panel_top},
    };

    std::vector<WalkableEdge> edges;
    append_window_edges(candidates, 64, edges);

    REQUIRE(edges.size() == 1);
    CHECK(edges[0].kind == EdgeKind::panel_top);
}
