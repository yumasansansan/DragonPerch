// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_clock.hpp"

#include "wayland_display.hpp"

#include <wayland-client.h>

namespace dp::wl {

FrameClock::FrameClock(WaylandDisplay& display, wl_surface* surface)
    : display_(&display), surface_(surface)
{
}

FrameClock::~FrameClock()
{
    if (pending_ != nullptr) {
        wl_callback_destroy(pending_);
    }
}

void FrameClock::done(void* data, wl_callback* callback, std::uint32_t)
{
    auto* self = static_cast<FrameClock*>(data);
    wl_callback_destroy(callback);
    self->pending_ = nullptr;
    self->arrived_ = true;
}

std::chrono::nanoseconds FrameClock::wait_for_next_frame()
{
    static const wl_callback_listener listener = {.done = &FrameClock::done};

    if (pending_ == nullptr) {
        arrived_ = false;
        pending_ = wl_surface_frame(surface_);
        wl_callback_add_listener(pending_, &listener, this);

        // A frame request is only delivered by a commit, so one has to follow it. This
        // one attaches nothing, which re-commits the surface state and no new pixels --
        // the compositor answers it the next time it presents this surface, which is
        // exactly the moment the loop wants to wake up.
        wl_surface_commit(surface_);
    }

    while (!arrived_ && !disconnected_) {
        const WaylandDisplay::Dispatch result = display_->dispatch();
        if (result == WaylandDisplay::Dispatch::failed) {
            disconnected_ = true;
        } else if (result == WaylandDisplay::Dispatch::interrupted) {
            // Ctrl+C. Return without a frame so the caller can look at its stop flag; the
            // callback stays pending and is picked up if the loop carries on after all.
            return {};
        }
    }
    if (!arrived_) {
        // Left the wait because the compositor went away, not because it presented
        // anything. Counting this would make "0 frames presented" -- the line that says
        // nothing was ever drawn -- read as 1.
        return {};
    }
    arrived_ = false;
    ++frames_;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = have_previous_ ? now - previous_ : std::chrono::steady_clock::duration{};
    previous_ = now;
    have_previous_ = true;

    return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
}

} // namespace dp::wl
