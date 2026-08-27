// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/sprite_pack_file.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string_view>

using namespace dp;

namespace {

constexpr std::string_view minimal = R"(
[pack]
atlas = konqi.png
frame-width = 32
frame-height = 32

[walk]
frames = 0, 1, 2
duration = 100
)";

} // namespace

TEST_CASE("a pack maps frame numbers onto a grid of cells", "[pack]")
{
    // 128x64 at 32x32 is four columns and two rows, so frame 5 is the second cell of the
    // second row. Getting this wrong shows up as a dragon made of pieces of its neighbours.
    const SpritePackFile file = parse_sprite_pack(R"(
[pack]
atlas = konqi.png
frame-width = 32
frame-height = 32

[walk]
frames = 0, 3, 4, 5
duration = 100
)",
                                                  7, PixelSize{128, 64});

    CHECK(file.atlas_file == "konqi.png");
    CHECK(file.pack.atlas_id() == 7);

    const Animation& walk = file.pack.require("walk");
    REQUIRE(walk.frames().size() == 4);

    CHECK(walk.frames()[0].source == PixelRect{0, 0, 32, 32});
    CHECK(walk.frames()[1].source == PixelRect{96, 0, 32, 32});
    CHECK(walk.frames()[2].source == PixelRect{0, 32, 32, 32});
    CHECK(walk.frames()[3].source == PixelRect{32, 32, 32, 32});
}

TEST_CASE("the anchor defaults to the bottom centre of a cell", "[pack]")
{
    const SpritePackFile file = parse_sprite_pack(minimal, 0, PixelSize{96, 32});
    CHECK(file.pack.require("walk").frames()[0].anchor == PixelOffset{16, 32});
}

TEST_CASE("an explicit anchor overrides the default", "[pack]")
{
    const SpritePackFile file = parse_sprite_pack(R"(
[pack]
atlas = a.png
frame-width = 32
frame-height = 32

[walk]
frames = 0
duration = 100
anchor = 8, 30
)",
                                                  0, PixelSize{32, 32});

    CHECK(file.pack.require("walk").frames()[0].anchor == PixelOffset{8, 30});
}

TEST_CASE("durations are milliseconds", "[pack]")
{
    const SpritePackFile file = parse_sprite_pack(minimal, 0, PixelSize{96, 32});

    // Approx, not equality: the total is a sum of doubles, and three hundredths of a second
    // added up is not bit-identical to three tenths written down.
    CHECK(file.pack.require("walk").frames()[0].duration.count() == Catch::Approx(0.1));
    CHECK(file.pack.require("walk").total().count() == Catch::Approx(0.3));
}

TEST_CASE("animations loop unless told otherwise", "[pack]")
{
    const SpritePackFile file = parse_sprite_pack(R"(
[pack]
atlas = a.png
frame-width = 32
frame-height = 32

[walk]
frames = 0
duration = 100

[land]
frames = 0
duration = 100
loop = false
)",
                                                  0, PixelSize{32, 32});

    CHECK(file.pack.require("walk").loops());
    CHECK_FALSE(file.pack.require("land").loops());
}

TEST_CASE("comments and blank lines are ignored", "[pack]")
{
    // '#' is what most people type; ';' is what KConfig writes. Both have to work, or a
    // pack edited by hand and a pack written by a KDE tool would not read the same.
    const SpritePackFile file = parse_sprite_pack(R"(
# a hash comment

[pack]
atlas = a.png      ; trailing comment
frame-width = 32
frame-height = 32

[walk]   # here too
frames = 0
duration = 100
)",
                                                  0, PixelSize{32, 32});

    CHECK(file.atlas_file == "a.png");
    CHECK(file.pack.has("walk"));
}

TEST_CASE("the atlas filename can be read without the rest", "[pack]")
{
    // The caller has to decode the image before it knows the atlas id or the grid size, so
    // this pass has to work on its own.
    CHECK(parse_atlas_filename(minimal) == "konqi.png");
}

TEST_CASE("a malformed pack is refused rather than half-read", "[pack]")
{
    // A sprite pack is authored once and shipped. Silently mis-parsing one shows up as a
    // dragon standing in the wrong place, which is far harder to trace than a refusal.
    SECTION("no [pack] section")
    {
        CHECK_THROWS_AS(parse_sprite_pack("[walk]\nframes = 0\nduration = 1\n", 0,
                                          PixelSize{32, 32}),
                        std::runtime_error);
    }

    SECTION("a frame outside the grid")
    {
        CHECK_THROWS_AS(parse_sprite_pack(minimal, 0, PixelSize{32, 32}), std::runtime_error);
    }

    SECTION("a cell larger than the atlas")
    {
        CHECK_THROWS_AS(parse_sprite_pack(minimal, 0, PixelSize{16, 16}), std::runtime_error);
    }

    SECTION("a duration that is not a number")
    {
        CHECK_THROWS_AS(parse_sprite_pack(R"(
[pack]
atlas = a.png
frame-width = 32
frame-height = 32

[walk]
frames = 0
duration = soon
)",
                                          0, PixelSize{32, 32}),
                        std::runtime_error);
    }

    SECTION("no animations at all")
    {
        CHECK_THROWS_AS(parse_sprite_pack(R"(
[pack]
atlas = a.png
frame-width = 32
frame-height = 32
)",
                                          0, PixelSize{32, 32}),
                        std::runtime_error);
    }
}
