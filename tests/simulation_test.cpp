// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/placeholder_pack.hpp"
#include "dragonperch/simulation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace dp;

namespace {

/// A fake world is just a list of line segments. That is the whole point of the core/backend
/// split: physics is testable with no compositor, no windows, and no platform anywhere.
WorldSnapshot make_world(std::uint64_t version, std::vector<WalkableEdge> edges,
                         std::vector<OutputInfo> outputs = {})
{
    WorldSnapshot::sort(edges);
    return WorldSnapshot{version, std::move(edges), std::move(outputs)};
}

WalkableEdge shelf(std::int64_t owner, int y, int left, int right)
{
    return WalkableEdge{owner, y, left, right, EdgeKind::window_top, 0};
}

/// Runs the simulation at a fixed 60 Hz for a while, or until `done` says to stop.
template <typename Predicate>
bool run_until(Simulation& sim, Predicate done, int max_frames = 600)
{
    for (int i = 0; i < max_frames; ++i) {
        if (done()) {
            return true;
        }
        sim.update(Duration{1.0 / 60.0});
    }
    return done();
}

/// Records what the simulation asked to be drawn.
class RecordingRenderer final : public ISpriteRenderer {
public:
    int register_atlas(std::span<const std::byte>, PixelSize) override { return 0; }
    void begin_frame() override { draws.clear(); }
    void draw(const SpriteDraw& sprite) override { draws.push_back(sprite); }
    void end_frame() override { ++frames; }

    std::vector<SpriteDraw> draws;
    int frames = 0;
};

} // namespace

TEST_CASE("a pet in a world with no outputs does not fall for ever", "[simulation]")
{
    // There is no floor to be recycled at when there are no outputs, so falling used to add
    // to y for as long as it was asked to and stop only at signed overflow. Nothing is drawn
    // on a desktop with no outputs; standing still is the only bounded answer.
    //
    // Reachable for a moment whenever every monitor goes away at once. Found by reading the
    // fall path while writing the simulation fuzzer, which could not reach it: it needs
    // every world in a run to have no outputs at all, and libFuzzer never guessed one.
    const SpritePack pack = placeholder_pack::create(0);

    Simulation simulation;
    simulation.set_world(WorldSnapshot{1, {}, {}});
    simulation.spawn(pack, PixelPoint{100, 100});

    for (int i = 0; i < 2000; ++i) {
        simulation.update(Duration{1.0});
    }

    REQUIRE(simulation.pets().size() == 1);

    // Still falling, and still somewhere an int can hold. Two thousand seconds at terminal
    // velocity is 1.8 million pixels; without the bound this would be most of the way to
    // signed overflow, and a longer run would be all of it.
    CHECK(simulation.pets()[0].position().y <= 1'000'000);
    CHECK(simulation.pets()[0].position().x == 100);
}

TEST_CASE("a falling pet lands on the edge, not near it", "[simulation]")
{
    const SpritePack pack = placeholder_pack::create(0);
    Simulation sim;
    sim.set_world(make_world(1, {shelf(42, 500, 0, 1000)}));

    Pet& pet = sim.spawn(pack, PixelPoint{100, 0});
    REQUIRE(pet.state() == PetState::falling);

    const bool landed = run_until(sim, [&] { return sim.pets()[0].perch().has_value(); });
    REQUIRE(landed);

    // Exactly on the row, not a pixel above or below: this is the number a viewer notices.
    CHECK(sim.pets()[0].position().y == 500);
    CHECK(sim.pets()[0].perch() == 42);
    CHECK(sim.pets()[0].velocity_y() == 0.0);
}

TEST_CASE("a fast fall cannot tunnel through a thin edge", "[simulation]")
{
    const SpritePack pack = placeholder_pack::create(0);

    SimulationOptions options;
    options.terminal_velocity = 5000.0; // far more than one frame's worth of screen
    Simulation sim{options};
    sim.set_world(make_world(1, {shelf(1, 900, 0, 1000)}));

    sim.spawn(pack, PixelPoint{100, 0});

    // A point test at the destination would step straight past the shelf at this speed.
    const bool landed = run_until(sim, [&] { return sim.pets()[0].perch().has_value(); });
    REQUIRE(landed);
    CHECK(sim.pets()[0].position().y == 900);
}

TEST_CASE("an edge narrower than the minimum is not a perch", "[simulation]")
{
    const SpritePack pack = placeholder_pack::create(0);

    SimulationOptions options;
    options.minimum_perch_width = 64;
    Simulation sim{options};

    // 40 wide: a tooltip, not a title bar.
    sim.set_world(make_world(1, {shelf(1, 500, 80, 120)}));
    sim.spawn(pack, PixelPoint{100, 0});

    for (int i = 0; i < 120; ++i) {
        sim.update(Duration{1.0 / 60.0});
    }

    CHECK_FALSE(sim.pets()[0].perch().has_value());
    CHECK(sim.pets()[0].position().y > 500);
}

TEST_CASE("a pet rides the window it stands on", "[simulation]")
{
    const SpritePack pack = placeholder_pack::create(0);
    Simulation sim;
    sim.set_world(make_world(1, {shelf(7, 500, 0, 1000)}));
    sim.spawn(pack, PixelPoint{100, 0});

    REQUIRE(run_until(sim, [&] { return sim.pets()[0].perch().has_value(); }));
    const int x_before = sim.pets()[0].position().x;

    // The window moves up. Same owner id, new geometry -- which is exactly why perches are
    // tracked by identity rather than by position.
    sim.set_world(make_world(2, {shelf(7, 300, 0, 1000)}));
    sim.update(Duration{1.0 / 60.0});

    CHECK(sim.pets()[0].position().y == 300);
    CHECK(sim.pets()[0].perch() == 7);
    CHECK(std::abs(sim.pets()[0].position().x - x_before) <= 2);
}

TEST_CASE("a pet whose window vanishes falls", "[simulation]")
{
    const SpritePack pack = placeholder_pack::create(0);
    Simulation sim;
    sim.set_world(make_world(1, {shelf(7, 500, 0, 1000)}));
    sim.spawn(pack, PixelPoint{100, 0});

    REQUIRE(run_until(sim, [&] { return sim.pets()[0].perch().has_value(); }));

    sim.set_world(make_world(2, {})); // window closed
    sim.update(Duration{1.0 / 60.0});

    CHECK_FALSE(sim.pets()[0].perch().has_value());
    CHECK(sim.pets()[0].state() == PetState::falling);
}

TEST_CASE("a pet that walks off the end stops being perched", "[simulation]")
{
    const SpritePack pack = placeholder_pack::create(0);

    SimulationOptions options;
    options.turn_at_edge_chance = 0.0; // always step off, so the test is not a coin flip
    options.idle_interval = Duration::zero();
    Simulation sim{options};

    // With a floor under it, because a pet only steps off when there is something below
    // to land on -- over a void it turns round instead, which is what the shelf on its own
    // used to test by accident.
    sim.set_world(make_world(1, {shelf(1, 500, 0, 200), shelf(2, 900, -1000, 1000)}));
    sim.spawn(pack, PixelPoint{190, 0});

    REQUIRE(run_until(sim, [&] { return sim.pets()[0].perch() == 1; }));
    REQUIRE(run_until(sim, [&] { return sim.pets()[0].perch() != 1; }));

    CHECK(sim.pets()[0].state() == PetState::falling);
}

TEST_CASE("a pet that turns back stays on its perch", "[simulation]")
{
    const SpritePack pack = placeholder_pack::create(0);

    SimulationOptions options;
    options.turn_at_edge_chance = 1.0; // always turn
    options.idle_interval = Duration::zero();
    Simulation sim{options};

    sim.set_world(make_world(1, {shelf(1, 500, 0, 200)}));
    sim.spawn(pack, PixelPoint{190, 0});

    REQUIRE(run_until(sim, [&] { return sim.pets()[0].perch().has_value(); }));

    const int facing_before = sim.pets()[0].facing();
    REQUIRE(run_until(sim, [&] { return sim.pets()[0].facing() != facing_before; }));

    CHECK(sim.pets()[0].perch() == 1);
    CHECK(sim.pets()[0].position().x >= 0);
    CHECK(sim.pets()[0].position().x < 200);
}

TEST_CASE("the feet stay put when a pet turns round", "[simulation]")
{
    // The anchor is authored for a right-facing sprite. If it is not mirrored along with
    // the sprite, the feet jump sideways by twice the anchor's distance from centre every
    // time the pet changes direction -- subtle in code, obvious on screen.
    //
    // The placeholder pack cannot detect that: its anchor sits exactly at the centre of the
    // cell, so mirroring it is a no-op. This builds a pack with a deliberately off-centre
    // anchor, where a missing mirror moves the feet by 24 pixels.
    constexpr int cell = 40;
    constexpr int anchor_x = 8;

    std::map<std::string, Animation, std::less<>> animations;
    for (const char* name : {"walk", "idle", "turn", "fall", "land", "fly"}) {
        animations.emplace(name,
                           Animation{name,
                                     {AnimationFrame{PixelRect{0, 0, cell, cell},
                                                     PixelOffset{anchor_x, cell}, Duration{0.1}}},
                                     true});
    }
    const SpritePack pack{"offset-anchor", "Off-centre anchor", "GPL-3.0-or-later", "test", 0,
                          std::move(animations)};

    SimulationOptions options;
    options.turn_at_edge_chance = 1.0;
    options.idle_interval = Duration::zero();
    Simulation sim{options};

    sim.set_world(make_world(1, {shelf(1, 500, 0, 400)}));
    sim.spawn(pack, PixelPoint{390, 0});

    RecordingRenderer renderer;

    // Sample while the pet is turning, and again once the turn has completed. A pet does
    // not move during a turn, so the two samples are at the same position and any
    // difference in where the feet land comes from the anchor alone.
    REQUIRE(run_until(sim, [&] { return sim.pets()[0].state() == PetState::turning; }));

    const PixelPoint position_before = sim.pets()[0].position();
    const int facing_before = sim.pets()[0].facing();

    sim.render(renderer);
    REQUIRE(renderer.draws.size() == 1);
    const SpriteDraw before = renderer.draws.front();

    REQUIRE(run_until(sim, [&] { return sim.pets()[0].facing() != facing_before; }));
    REQUIRE(sim.pets()[0].position() == position_before);

    sim.render(renderer);
    REQUIRE(renderer.draws.size() == 1);
    const SpriteDraw after = renderer.draws.front();

    CHECK(before.flip_x != after.flip_x);

    // Where the sprite's own anchor point ends up on screen, in each facing.
    const int feet_before = before.destination.x + (before.flip_x ? cell - anchor_x : anchor_x);
    const int feet_after = after.destination.x + (after.flip_x ? cell - anchor_x : anchor_x);

    CHECK(feet_before == position_before.x);
    CHECK(feet_after == position_before.x);
}

TEST_CASE("a pack that draws both directions is not mirrored", "[simulation]")
{
    // Konqi carries KDE's K, so his pack draws left-facing frames rather than letting the
    // renderer mirror the right-facing ones. Two things have to hold for that to work: the
    // left frames get used when the pet faces left, and the renderer leaves them alone.
    constexpr int cell = 40;

    std::map<std::string, Animation, std::less<>> animations;
    for (const char* name : {"walk", "idle", "turn", "fall", "land", "fly"}) {
        animations.emplace(
            name, Animation{name,
                            {AnimationFrame{PixelRect{0, 0, cell, cell},
                                            PixelOffset{cell / 2, cell}, Duration{0.1}}},
                            true,
                            {AnimationFrame{PixelRect{cell, 0, cell, cell},
                                            PixelOffset{cell / 2, cell}, Duration{0.1}}}});
    }
    const SpritePack pack{"two-way", "Two directions", "CC-BY-SA-4.0", "test", 0,
                          std::move(animations)};

    SimulationOptions options;
    options.turn_at_edge_chance = 1.0;
    options.idle_interval = Duration::zero();
    Simulation sim{options};

    sim.set_world(make_world(1, {shelf(1, 500, 0, 400)}));
    sim.spawn(pack, PixelPoint{390, 0});

    RecordingRenderer renderer;
    const auto sample = [&] {
        sim.render(renderer);
        REQUIRE(renderer.draws.size() == 1);
        return renderer.draws.front();
    };

    REQUIRE(run_until(sim, [&] { return sim.pets()[0].perch().has_value(); }));

    const int facing_before = sim.pets()[0].facing();
    const SpriteDraw before = sample();

    REQUIRE(run_until(sim, [&] { return sim.pets()[0].facing() != facing_before; }));
    const SpriteDraw after = sample();

    // Never mirrored, whichever way round the pet is: mirroring is what would reverse the K.
    CHECK_FALSE(before.flip_x);
    CHECK_FALSE(after.flip_x);

    // Different artwork for each direction, rather than the same cell twice.
    CHECK(before.source != after.source);

    const PixelRect right{0, 0, cell, cell};
    const PixelRect left{cell, 0, cell, cell};
    CHECK((facing_before > 0 ? before.source : after.source) == right);
    CHECK((facing_before > 0 ? after.source : before.source) == left);
}

TEST_CASE("the simulation is deterministic for a given seed", "[simulation]")
{
    const SpritePack pack = placeholder_pack::create(0);

    const auto run = [&pack](std::uint32_t seed) {
        SimulationOptions options;
        options.seed = seed;
        Simulation sim{options};
        // Two shelves rather than one: on a single shelf over a void the pet never steps
        // off, so it paces the same path whatever the seed and the seeds cannot differ.
        sim.set_world(make_world(1, {shelf(1, 500, 0, 300), shelf(2, 900, -1000, 1000)}));
        sim.spawn(pack, PixelPoint{100, 0});
        for (int i = 0; i < 900; ++i) {
            sim.update(Duration{1.0 / 60.0});
        }
        return sim.pets()[0].position();
    };

    CHECK(run(1234) == run(1234));
    CHECK_FALSE(run(1234) == run(9876));
}

TEST_CASE("rendering emits one sprite per pet and nothing else", "[simulation]")
{
    const SpritePack pack = placeholder_pack::create(3);
    Simulation sim;
    sim.set_world(make_world(1, {shelf(1, 500, 0, 1000)}));

    sim.spawn(pack, PixelPoint{100, 0});
    sim.spawn(pack, PixelPoint{300, 0});

    RecordingRenderer renderer;
    sim.render(renderer);

    CHECK(renderer.frames == 1);
    REQUIRE(renderer.draws.size() == 2);
    for (const SpriteDraw& draw : renderer.draws) {
        CHECK(draw.atlas_id == 3);
        CHECK_FALSE(draw.source.empty());
    }
}
