// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/host.hpp"

#include <algorithm>
#include <chrono>

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

    while (!should_stop()) {
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

    // The handler captures `this`; drop it before the host goes away.
    world_->set_changed_handler(nullptr);
}

} // namespace dp
