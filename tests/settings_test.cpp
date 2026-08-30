// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/ini.hpp"
#include "dragonperch/settings.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

using namespace dp;

TEST_CASE("an empty file is every default", "[settings]")
{
    const Settings settings = parse_settings("");
    CHECK(settings == Settings{});
}

TEST_CASE("settings survive a round trip", "[settings]")
{
    // The whole point of writing the file out: what the settings program saves has to come
    // back as what it saved, including the values that happen to be the defaults.
    Settings written;
    written.pets_per_mascot = 4;
    written.mascots = {"konqi", "kori"};
    written.walk_speed = 63.5;
    written.idle_interval = 0.0;
    written.outputs = {"DP-1"};
    written.pause_for_fullscreen = false;

    CHECK(parse_settings(write_settings(written)) == written);
}

TEST_CASE("a value that cannot be read keeps its default", "[settings]")
{
    // This file is edited by hand and written by two other programs. Losing every setting
    // because one line is wrong is worse than losing the one line.
    const Settings settings = parse_settings(R"(
[DragonPerch]
pets-per-mascot = lots
walk-speed = 55
mascots = kori
)");

    CHECK(settings.pets_per_mascot == Settings{}.pets_per_mascot);
    CHECK(settings.walk_speed == Catch::Approx(55.0));
    CHECK(settings.mascots == std::vector<std::string>{"kori"});
}

TEST_CASE("a malformed file is every default rather than a refusal", "[settings]")
{
    // A dragon that will not appear because of a stray bracket is worse than a dragon that
    // appears with the defaults. The parser itself does throw -- that is checked below --
    // and reading settings is where it is caught.
    CHECK(parse_settings("[unterminated\npets-per-mascot = 9") == Settings{});
}

TEST_CASE("values are clamped rather than taken as written", "[settings]")
{
    // Nothing downstream checks these. A walk speed of ten million is a pet that crosses
    // the screen between two frames and is never seen again.
    const Settings settings = parse_settings(R"(
[DragonPerch]
pets-per-mascot = 5000
walk-speed = 0
idle-interval = -3
)");

    CHECK(settings.pets_per_mascot == 64);
    CHECK(settings.walk_speed == Catch::Approx(1.0));
    CHECK(settings.idle_interval == Catch::Approx(0.0));
}

TEST_CASE("an empty list means everything, not nothing", "[settings]")
{
    // The difference matters: somebody who has never opened the settings has an empty
    // mascot list and should get every dragon, not none.
    const Settings all = parse_settings("[DragonPerch]\nmascots =\n");
    CHECK(all.mascots.empty());
    CHECK(all.wants_mascot("konqi"));
    CHECK(all.wants_output("DP-1"));

    const Settings some = parse_settings("[DragonPerch]\nmascots = katie, kori\n");
    CHECK_FALSE(some.wants_mascot("konqi"));
    CHECK(some.wants_mascot("kori"));
}

TEST_CASE("only a change to the cast needs the pets spawning again", "[settings]")
{
    // Adjusting a speed while a pet is mid-stride should not teleport it, and changing
    // which dragons exist cannot be done any other way.
    const Settings before;

    Settings faster = before;
    faster.walk_speed = 90.0;
    CHECK_FALSE(before.needs_respawn(faster));

    Settings fewer = before;
    fewer.pets_per_mascot = before.pets_per_mascot + 1;
    CHECK(before.needs_respawn(fewer));

    Settings others = before;
    others.mascots = {"katie"};
    CHECK(before.needs_respawn(others));
}

TEST_CASE("the settings only reach the tuning they are allowed to", "[settings]")
{
    // SimulationOptions carries knobs meant for whoever writes the physics, not whoever
    // runs the program. A settings file must not be able to move them.
    Settings settings;
    settings.walk_speed = 70.0;
    settings.idle_interval = 2.0;

    const SimulationOptions options = settings.to_options();
    CHECK(options.walk_speed == Catch::Approx(70.0));
    CHECK(options.idle_interval.count() == Catch::Approx(2.0));

    const SimulationOptions untouched;
    CHECK(options.gravity == Catch::Approx(untouched.gravity));
    CHECK(options.terminal_velocity == Catch::Approx(untouched.terminal_velocity));
    CHECK(options.turn_at_edge_chance == Catch::Approx(untouched.turn_at_edge_chance));
    CHECK(options.minimum_perch_width == untouched.minimum_perch_width);
}

TEST_CASE("both comment markers work, and a key can hold one", "[ini]")
{
    // '#' is what people type and ';' is what KConfig writes, so a file round-tripped
    // through the KDE settings program has to read the same afterwards.
    const std::vector<ini::Section> sections = ini::parse(R"(
# a hash comment
[one]
a = 1   ; trailing
b = 2   # trailing

[two]
c = 3
)");

    REQUIRE(sections.size() == 2);
    CHECK(sections[0].name == "one");
    REQUIRE(sections[0].find("a") != nullptr);
    CHECK(sections[0].find("a")->value == "1");
    CHECK(sections[0].find("b")->value == "2");
    CHECK(sections[0].find("c") == nullptr);

    REQUIRE(ini::find(sections, "two", "c") != nullptr);
    CHECK(ini::find(sections, "two", "c")->value == "3");
    CHECK(ini::find(sections, "three", "c") == nullptr);
}

TEST_CASE("the parser refuses what it cannot make sense of", "[ini]")
{
    CHECK_THROWS_AS(ini::parse("[unterminated\n"), std::runtime_error);
    CHECK_THROWS_AS(ini::parse("[a]\nno equals sign\n"), std::runtime_error);
    CHECK_THROWS_AS(ini::parse("key = value\n"), std::runtime_error);
}

TEST_CASE("a line number travels with every value", "[ini]")
{
    // It is what lets a complaint about a sprite pack name the line rather than the file.
    const std::vector<ini::Section> sections = ini::parse("\n\n[a]\n\nk = v\n");
    REQUIRE(sections.size() == 1);
    CHECK(sections[0].line == 3);
    REQUIRE(sections[0].find("k") != nullptr);
    CHECK(sections[0].find("k")->line == 5);
}

TEST_CASE("a walk speed that is not a number is refused", "[settings]")
{
    // strtod accepts "nan" and "inf", and std::clamp passes a NaN straight through: both
    // comparisons inside it are false. A NaN walk speed reaches std::lround in the
    // simulation, where it is undefined behaviour rather than a slow dragon.
    const Settings settings = parse_settings(R"(
[DragonPerch]
walk-speed = nan
idle-interval = inf
)");

    CHECK(settings.walk_speed == Catch::Approx(Settings{}.walk_speed));
    CHECK(settings.idle_interval == Catch::Approx(Settings{}.idle_interval));
}
