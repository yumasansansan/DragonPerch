// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/render.hpp"
#include "dragonperch/simulation.hpp"
#include "dragonperch/world.hpp"

#include <functional>
#include <mutex>

namespace dp {

/// Wires the three backend-supplied pieces together and runs the loop.
///
/// Platform heads construct their own trio and call `run`. Nothing here knows what a
/// window is.
class PetHost {
public:
    PetHost(Simulation& simulation, IWorldProvider& world, ISpriteRenderer& renderer,
            IFrameClock& clock);

    /// Runs until `should_stop` returns true. Blocks; the frame clock does the waiting.
    void run(const std::function<bool()>& should_stop);

private:
    /// A hidden or occluded surface can be starved of frame callbacks for a long time. When
    /// it comes back, integrating a single thirty-second step would fling every pet off the
    /// bottom of the screen, so the step is capped.
    static constexpr Duration max_step{0.25};

    /// The provider publishes from its own thread; the simulation is only touched from the
    /// loop. This is the handover point, and the only shared state between the two.
    std::mutex pending_mutex_;
    WorldSnapshot pending_world_;
    bool has_pending_ = false;

    Simulation* simulation_;
    IWorldProvider* world_;
    ISpriteRenderer* renderer_;
    IFrameClock* clock_;
};

} // namespace dp
