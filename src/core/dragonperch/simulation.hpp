// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"
#include "dragonperch/render.hpp"
#include "dragonperch/sprites.hpp"
#include "dragonperch/world.hpp"

#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace dp {

enum class PetState {
    walking,
    idle,
    turning,
    falling,
    landing,

    /// Nothing enters this yet; see EdgeKind::screen_ceiling. A pack's `fly` animation is
    /// therefore never asked for, which is why a pack missing one still runs.
    flying,
};

[[nodiscard]] std::string_view animation_for(PetState state) noexcept;

/// One dragon.
class Pet {
public:
    Pet(int id, const SpritePack& pack, PixelPoint position);

    [[nodiscard]] int id() const noexcept { return id_; }
    [[nodiscard]] const SpritePack& pack() const noexcept { return *pack_; }

    /// Feet position, in the shared desktop space.
    [[nodiscard]] PixelPoint position() const noexcept { return position_; }

    [[nodiscard]] PetState state() const noexcept { return state_; }

    /// +1 facing right, -1 facing left.
    [[nodiscard]] int facing() const noexcept { return facing_; }

    /// Vertical velocity in px/s. Only meaningful while falling.
    [[nodiscard]] double velocity_y() const noexcept { return velocity_y_; }

    /// The edge this pet stands on, or empty while airborne.
    ///
    /// Tracked by owner id rather than by position, so a pet riding a window that gets
    /// dragged is carried along instead of being dropped and re-acquired every frame.
    [[nodiscard]] std::optional<std::int64_t> perch() const noexcept { return perch_; }

    [[nodiscard]] Duration state_elapsed() const noexcept { return state_elapsed_; }

    /// The animation for the state the pet is in.
    ///
    /// Resolved when the state changes rather than when it is asked for. It used to be a
    /// `std::map` lookup keyed on a string, run twice per pet per frame -- once here and
    /// once through `current_frame` -- to answer a question whose answer only changes when
    /// a pet starts walking or stops.
    [[nodiscard]] const Animation& current_animation() const noexcept { return *animation_; }
    [[nodiscard]] const AnimationFrame& current_frame() const;

private:
    friend class Simulation;

    void advance(Duration dt) noexcept;

    /// Throws if the pack has no animation for `state` -- which is the documented moment
    /// for that: the first time a pet enters a state the pack cannot draw.
    void enter(PetState state);

    int id_ = 0;
    const SpritePack* pack_ = nullptr;
    const Animation* animation_ = nullptr;
    PixelPoint position_{};
    PetState state_ = PetState::falling;
    int facing_ = 1;
    double velocity_y_ = 0.0;
    std::optional<std::int64_t> perch_;
    Duration state_elapsed_{};
    Duration animation_elapsed_{};
};

struct SimulationOptions {
    double gravity = 1400.0;            ///< px/s^2
    double walk_speed = 42.0;           ///< px/s
    double terminal_velocity = 900.0;   ///< px/s, so a long fall cannot tunnel thin edges
    Duration idle_interval{9.0};        ///< mean time between spontaneous pauses
    Duration idle_duration{2.5};
    Duration turn_duration{0.35};
    Duration land_duration{0.2};

    /// Edges narrower than this are not perches. Keeps pets off tooltip slivers.
    int minimum_perch_width = 64;

    /// Chance of turning back rather than stepping off when a walk reaches an edge's end.
    double turn_at_edge_chance = 0.65;

    std::uint32_t seed = 0x4b4f4e51; // 'KONQ'
};

/// The whole behaviour of the app, with no platform knowledge whatsoever.
///
/// Feed it a WorldSnapshot and a delta time; read sprites back out. This is the piece that
/// stays testable with no compositor anywhere in sight, because a fake world is just a list
/// of line segments.
class Simulation {
public:
    explicit Simulation(SimulationOptions options = {});

    [[nodiscard]] std::span<const Pet> pets() const noexcept { return pets_; }
    [[nodiscard]] const SimulationOptions& options() const noexcept { return options_; }

    Pet& spawn(const SpritePack& pack, PixelPoint at);

    /// Removes every pet. What a settings change that alters *which* dragons there are has
    /// to do -- adjusting a walk speed does not disturb anybody, and changing the cast
    /// does. The head respawns afterwards, because only it knows which packs it has.
    void clear_pets() noexcept;

    /// Replaces the tuning. Takes effect on the next update; nothing is re-simulated, so a
    /// pet mid-stride keeps its stride and takes the new speed from here on.
    ///
    /// The seed is deliberately not re-read: reseeding on every settings change would make
    /// the behaviour depend on how often somebody opened the settings.
    void set_options(const SimulationOptions& options) noexcept;

    void set_world(WorldSnapshot world);
    [[nodiscard]] const WorldSnapshot& world() const noexcept { return world_; }

    void update(Duration dt);
    void render(ISpriteRenderer& renderer) const;

private:
    void reconcile_perch(Pet& pet);
    void update_walking(Pet& pet, double seconds);
    void update_falling(Pet& pet, double seconds);
    static void drop(Pet& pet);

    SimulationOptions options_;
    std::vector<Pet> pets_;
    std::mt19937 random_;

    WorldSnapshot world_;
    std::uint64_t last_world_version_ = 0;
    bool seen_world_ = false;
};

} // namespace dp
