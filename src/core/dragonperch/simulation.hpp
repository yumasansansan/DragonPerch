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

    [[nodiscard]] const Animation& current_animation() const;
    [[nodiscard]] const AnimationFrame& current_frame() const;

private:
    friend class Simulation;

    void advance(Duration dt) noexcept;
    void enter(PetState state) noexcept;

    int id_ = 0;
    const SpritePack* pack_ = nullptr;
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

    void set_world(WorldSnapshot world);
    [[nodiscard]] const WorldSnapshot& world() const noexcept { return world_; }

    void update(Duration dt);
    void render(ISpriteRenderer& renderer) const;

private:
    void reconcile_perch(Pet& pet);
    void update_walking(Pet& pet, double seconds);
    void update_falling(Pet& pet, double seconds);
    static void drop(Pet& pet) noexcept;

    SimulationOptions options_;
    std::vector<Pet> pets_;
    std::mt19937 random_;

    WorldSnapshot world_;
    std::uint64_t last_world_version_ = 0;
    bool seen_world_ = false;
};

} // namespace dp
