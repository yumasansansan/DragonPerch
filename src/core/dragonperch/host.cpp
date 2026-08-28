// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/host.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace dp {

PetHost::PetHost(Simulation& simulation, IWorldProvider& world, ISpriteRenderer& renderer,
                 IFrameClock& clock)
    : simulation_(&simulation)
    , world_(&world)
    , renderer_(&renderer)
    , clock_(&clock)
{
}

void PetHost::run(const std::function<bool()>& should_stop)
{
    world_->start();
    simulation_->set_world(world_->current());

    // The provider publishes from its own thread, so the handler cannot touch the
    // simulation directly. It hands the snapshot over and the loop below picks it up on the
    // next frame, which is also the only moment the simulation is safe to write to.
    world_->set_changed_handler([this](const WorldSnapshot& snapshot) {
        const std::lock_guard lock(pending_mutex_);
        pending_world_ = snapshot;
        has_pending_ = true;
    });

    // It captures `this`, and leaving by exception used to skip clearing it -- which left
    // the provider calling into a PetHost that was about to be destroyed. The exception
    // paths are the ones nobody exercises, so it cannot be a line at the end of the
    // function.
    struct ClearOnTheWayOut {
        IWorldProvider* world;
        ~ClearOnTheWayOut() { world->set_changed_handler(nullptr); }
    } clear{world_};

    while (!should_stop()) {
        if (paused_.load(std::memory_order_relaxed)) {
            // Deliberately not waiting on the frame clock. On Wayland that would mean
            // asking the compositor to tell us when to draw something we are not going to
            // draw, and a pause that still wakes at the refresh rate is not much of a
            // pause.
            std::this_thread::sleep_for(pause_poll);
            continue;
        }

        // The first step after a pause is however long the pause was, and max_step is what
        // stops that flinging every pet off the bottom of the screen.
        const Duration dt = std::min(
            std::chrono::duration_cast<Duration>(clock_->wait_for_next_frame()), max_step);

        {
            const std::lock_guard lock(pending_mutex_);
            if (has_pending_) {
                simulation_->set_world(std::move(pending_world_));
                has_pending_ = false;
            }
        }

        simulation_->update(dt);
        simulation_->render(*renderer_);
    }
}

void PetHost::set_paused(bool paused) noexcept
{
    paused_.store(paused, std::memory_order_relaxed);
}

bool PetHost::paused() const noexcept
{
    return paused_.load(std::memory_order_relaxed);
}

} // namespace dp
