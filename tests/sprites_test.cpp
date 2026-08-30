// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/placeholder_pack.hpp"
#include "dragonperch/simulation.hpp"  // PetState, animation_for
#include "dragonperch/sprites.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

using namespace dp;

namespace {

AnimationFrame frame(int x, double seconds)
{
    return AnimationFrame{PixelRect{x, 0, 10, 10}, PixelOffset{5, 10}, Duration{seconds}};
}

} // namespace

TEST_CASE("a looping animation wraps", "[sprites]")
{
    const Animation walk{"walk", {frame(0, 1.0), frame(10, 1.0), frame(20, 1.0)}, true};

    CHECK(walk.total() == Duration{3.0});
    CHECK(walk.frame_at(Duration{0.0}).source.x == 0);
    CHECK(walk.frame_at(Duration{1.5}).source.x == 10);
    CHECK(walk.frame_at(Duration{2.9}).source.x == 20);

    SECTION("and keeps wrapping however long it has been running")
    {
        // A pet can idle for a long time. Wrapping must not depend on how many cycles have
        // passed, and must not take time proportional to them.
        CHECK(walk.frame_at(Duration{3.0}).source.x == 0);
        CHECK(walk.frame_at(Duration{3600.0}).source.x == 0);
        CHECK(walk.frame_at(Duration{3601.5}).source.x == 10);
    }
}

TEST_CASE("a non-looping animation holds on its last frame", "[sprites]")
{
    const Animation land{"land", {frame(0, 0.2), frame(10, 0.2)}, false};

    CHECK(land.frame_at(Duration{0.1}).source.x == 0);
    CHECK(land.frame_at(Duration{0.3}).source.x == 10);
    CHECK(land.frame_at(Duration{99.0}).source.x == 10);
}

TEST_CASE("an empty animation yields an empty frame rather than reading past the end", "[sprites]")
{
    const Animation nothing{"nothing", {}, true};
    CHECK(nothing.frame_at(Duration{1.0}).source.empty());
}

TEST_CASE("a missing animation is an error, not a silent default", "[sprites]")
{
    const SpritePack pack = placeholder_pack::create(0);

    CHECK(pack.has("walk"));
    CHECK_FALSE(pack.has("breathe-fire"));
    CHECK_THROWS_AS(pack.require("breathe-fire"), std::out_of_range);
}

TEST_CASE("the placeholder pack covers every state the simulation can be in", "[sprites]")
{
    const SpritePack pack = placeholder_pack::create(0);

    // If a state is added without artwork for it, the pet throws the first time it enters
    // that state -- which would show up as a crash mid-run rather than at startup.
    for (const PetState state : {PetState::walking, PetState::idle, PetState::turning,
                                 PetState::falling, PetState::landing, PetState::flying}) {
        INFO("state animation: " << animation_for(state));
        CHECK(pack.has(animation_for(state)));
    }
}

TEST_CASE("the placeholder atlas is the size it claims and is not blank", "[sprites]")
{
    const PixelSize size = placeholder_pack::atlas_size();
    const std::vector<std::byte> pixels = placeholder_pack::render_atlas();

    REQUIRE(pixels.size()
            == static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4U);

    std::size_t opaque = 0;
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != std::byte{0}) {
            ++opaque;
        }
    }

    // Every frame should have a pet in it. A blank atlas would render as nothing at all,
    // which is hard to tell from a broken renderer.
    CHECK(opaque > 100U * static_cast<std::size_t>(placeholder_pack::frame_count));
}

TEST_CASE("both directions of an animation run on one clock", "[sprites]")
{
    // A pack may draw its own left-facing frames rather than being mirrored. Turning round
    // must not restart the cycle: a walk that resets to frame zero on every turn reads as a
    // stumble. So the same elapsed time picks the same frame number either way round.
    const Animation walk{"walk",
                         {AnimationFrame{PixelRect{0, 0, 10, 10}, PixelOffset{5, 10}, Duration{0.1}},
                          AnimationFrame{PixelRect{10, 0, 10, 10}, PixelOffset{5, 10}, Duration{0.1}}},
                         true,
                         {AnimationFrame{PixelRect{20, 0, 10, 10}, PixelOffset{5, 10}, Duration{0.1}},
                          AnimationFrame{PixelRect{30, 0, 10, 10}, PixelOffset{5, 10}, Duration{0.1}}}};

    CHECK(walk.has_left_frames());
    CHECK(walk.frame_at(Duration{0.05}, false).source == PixelRect{0, 0, 10, 10});
    CHECK(walk.frame_at(Duration{0.05}, true).source == PixelRect{20, 0, 10, 10});
    CHECK(walk.frame_at(Duration{0.15}, false).source == PixelRect{10, 0, 10, 10});
    CHECK(walk.frame_at(Duration{0.15}, true).source == PixelRect{30, 0, 10, 10});
}

TEST_CASE("an animation with no left frames falls back to the right ones", "[sprites]")
{
    // Which is what tells the renderer to mirror instead. Artwork without lettering on it
    // is better served by half as many cells.
    const Animation walk{"walk",
                         {AnimationFrame{PixelRect{0, 0, 10, 10}, PixelOffset{5, 10}, Duration{0.1}}},
                         true};

    CHECK_FALSE(walk.has_left_frames());
    CHECK(walk.frame_at(Duration{0.05}, true).source == PixelRect{0, 0, 10, 10});
}
