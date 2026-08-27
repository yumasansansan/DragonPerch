// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace dp {

std::string_view animation_for(PetState state) noexcept
{
    switch (state) {
    case PetState::walking:
        return "walk";
    case PetState::turning:
        return "turn";
    case PetState::falling:
        return "fall";
    case PetState::landing:
        return "land";
    case PetState::flying:
        return "fly";
    case PetState::idle:
        break;
    }
    return "idle";
}

// ------------------------------------------------------------------------------- Pet

Pet::Pet(int id, const SpritePack& pack, PixelPoint position)
    : id_(id)
    , pack_(&pack)
    , position_(position)
{
}

void Pet::advance(Duration dt) noexcept
{
    state_elapsed_ += dt;
    animation_elapsed_ += dt;
}

void Pet::enter(PetState state) noexcept
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    state_elapsed_ = Duration::zero();
    animation_elapsed_ = Duration::zero();
}

const AnimationFrame& Pet::current_frame() const
{
    return pack_->require(animation_for(state_)).frame_at(animation_elapsed_);
}

// ------------------------------------------------------------------------ Simulation

Simulation::Simulation(SimulationOptions options)
    : options_(options)
    , random_(options.seed)
{
}

Pet& Simulation::spawn(const SpritePack& pack, PixelPoint at)
{
    // Pets are held by value and the vector may reallocate, so nothing may cache a Pet*
    // across a spawn. Callers get a reference that is valid until the next one.
    pets_.emplace_back(static_cast<int>(pets_.size()), pack, at);
    return pets_.back();
}

void Simulation::set_world(WorldSnapshot world)
{
    world_ = std::move(world);
}

void Simulation::update(Duration dt)
{
    const bool world_changed = !seen_world_ || world_.version() != last_world_version_;
    last_world_version_ = world_.version();
    seen_world_ = true;

    const double seconds = dt.count();

    for (Pet& pet : pets_) {
        pet.advance(dt);

        if (world_changed) {
            reconcile_perch(pet);
        }

        switch (pet.state_) {
        case PetState::walking:
            update_walking(pet, seconds);
            break;

        case PetState::falling:
            update_falling(pet, seconds);
            break;

        case PetState::idle:
            if (pet.state_elapsed_ >= options_.idle_duration) {
                pet.enter(PetState::walking);
            }
            break;

        case PetState::turning:
            if (pet.state_elapsed_ >= options_.turn_duration) {
                pet.facing_ = -pet.facing_;
                pet.enter(PetState::walking);
            }
            break;

        case PetState::landing:
            if (pet.state_elapsed_ >= options_.land_duration) {
                pet.enter(PetState::walking);
            }
            break;

        case PetState::flying:
            // TODO: Konqi has wings. Flight is the obvious thing this can do that
            // XPenguins could not -- a pet that runs out of floor should be able to launch
            // and glide to another window instead of always plummeting.
            pet.enter(PetState::falling);
            break;
        }
    }
}

void Simulation::render(ISpriteRenderer& renderer) const
{
    renderer.begin_frame();

    for (const Pet& pet : pets_) {
        const AnimationFrame& frame = pet.current_frame();
        if (frame.source.empty()) {
            continue;
        }

        // The anchor is authored for a right-facing sprite. Mirror it when flipped, or the
        // feet shift by the anchor's distance from centre and the pet appears to hop
        // sideways every time it turns.
        const int anchor_x =
            pet.facing() >= 0 ? frame.anchor.dx : frame.source.width - frame.anchor.dx;

        renderer.draw(SpriteDraw{
            .atlas_id = pet.pack().atlas_id(),
            .source = frame.source,
            .destination = PixelPoint{pet.position().x - anchor_x,
                                      pet.position().y - frame.anchor.dy},
            .flip_x = pet.facing() < 0,
            .opacity = 1.0F,
        });
    }

    renderer.end_frame();
}

void Simulation::reconcile_perch(Pet& pet)
{
    if (!pet.perch_.has_value()) {
        return;
    }

    const WalkableEdge* edge = world_.find_by_owner(*pet.perch_);
    if (edge == nullptr || !edge->contains_x(pet.position_.x)
        || edge->width() < options_.minimum_perch_width) {
        drop(pet);
        return;
    }

    // The window moved: ride it rather than being left standing in mid-air.
    pet.position_.y = edge->y;
}

void Simulation::update_walking(Pet& pet, double seconds)
{
    if (!pet.perch_.has_value()) {
        drop(pet);
        return;
    }

    const WalkableEdge* edge = world_.find_by_owner(*pet.perch_);
    if (edge == nullptr) {
        drop(pet);
        return;
    }

    const int next_x =
        pet.position_.x + static_cast<int>(std::lround(options_.walk_speed * seconds * pet.facing_));

    if (!edge->contains_x(next_x)) {
        // Reached the end of the title bar: turn back, or step off.
        std::uniform_real_distribution<double> coin(0.0, 1.0);
        if (coin(random_) < options_.turn_at_edge_chance) {
            pet.enter(PetState::turning);
        } else {
            pet.position_.x = next_x;
            drop(pet);
        }
        return;
    }

    pet.position_.x = next_x;

    // Poisson-ish: the chance of pausing in this frame is dt over the mean interval, so the
    // behaviour does not change when the frame rate does.
    if (options_.idle_interval > Duration::zero()) {
        std::uniform_real_distribution<double> coin(0.0, 1.0);
        if (coin(random_) < seconds / options_.idle_interval.count()) {
            pet.enter(PetState::idle);
        }
    }
}

void Simulation::update_falling(Pet& pet, double seconds)
{
    pet.velocity_y_ =
        std::min(pet.velocity_y_ + (options_.gravity * seconds), options_.terminal_velocity);

    const int dy = static_cast<int>(std::lround(pet.velocity_y_ * seconds));
    const PixelPoint target{pet.position_.x, pet.position_.y + dy};

    // Swept, not a point test at the destination: at terminal velocity a frame covers more
    // than a title bar's height, and a point test would tunnel straight through.
    const WalkableEdge* landing = nullptr;
    for (const WalkableEdge& edge : world_.edges()) {
        if (edge.y <= pet.position_.y || edge.y > target.y) {
            continue;
        }
        if (!edge.contains_x(target.x) || edge.width() < options_.minimum_perch_width) {
            continue;
        }
        // Edges are sorted by y ascending, so the first hit is the highest one.
        landing = &edge;
        break;
    }

    if (landing != nullptr) {
        pet.position_ = PixelPoint{target.x, landing->y};
        pet.velocity_y_ = 0.0;
        pet.perch_ = landing->owner_id;
        pet.enter(PetState::landing);
        return;
    }

    pet.position_ = target;

    // Fell past the bottom of every output: reappear at the top of a random one.
    const std::span<const OutputInfo> outputs = world_.outputs();
    if (outputs.empty()) {
        return;
    }

    int floor = std::numeric_limits<int>::min();
    for (const OutputInfo& output : outputs) {
        floor = std::max(floor, output.bounds.bottom());
    }

    if (pet.position_.y > floor + 128) {
        std::uniform_int_distribution<std::size_t> pick(0, outputs.size() - 1);
        const OutputInfo& output = outputs[pick(random_)];

        std::uniform_int_distribution<int> across(output.bounds.left(), output.bounds.right() - 1);
        pet.position_ = PixelPoint{across(random_), output.bounds.top() - 64};
        pet.velocity_y_ = 0.0;
    }
}

void Simulation::drop(Pet& pet) noexcept
{
    pet.perch_.reset();
    pet.velocity_y_ = 0.0;
    pet.enter(PetState::falling);
}

} // namespace dp
