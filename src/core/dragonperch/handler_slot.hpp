// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/world.hpp"

#include <condition_variable>
#include <mutex>
#include <utility>

namespace dp {

/// Holds an IWorldProvider's changed-handler, and makes clearing it mean something.
///
/// Both providers publish from a thread of their own, and both used to copy the handler
/// under a lock and then call it outside one -- which is right, because holding a mutex
/// across a callback into somebody else's code is how deadlocks are built. What it left
/// open is the other end: `set_changed_handler(nullptr)` returned while a call was still
/// running, so a PetHost could clear its handler, return from `run`, and be destroyed while
/// the provider's thread was still inside a lambda that captured it.
///
/// So clearing waits. `set` does not return until no call to the previous handler is still
/// in flight, which is what "the handler will not be called again" has to mean if a caller
/// is going to destroy itself on the strength of it.
///
/// The handler must not call `set` from inside itself. That would be a thread waiting for
/// its own call to finish, and nothing here can rescue it.
class HandlerSlot {
public:
    void set(IWorldProvider::ChangedHandler handler)
    {
        std::unique_lock lock(mutex_);
        handler_ = std::move(handler);
        idle_.wait(lock, [this] { return in_flight_ == 0; });
    }

    void call(const WorldSnapshot& snapshot)
    {
        IWorldProvider::ChangedHandler handler;
        {
            const std::lock_guard lock(mutex_);
            if (!handler_) {
                return;
            }
            handler = handler_;
            ++in_flight_;
        }

        // Outside the lock, and the count is what a concurrent `set` waits on instead.
        handler(snapshot);

        {
            const std::lock_guard lock(mutex_);
            --in_flight_;
        }
        idle_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable idle_;
    IWorldProvider::ChangedHandler handler_;
    int in_flight_ = 0;
};

} // namespace dp
