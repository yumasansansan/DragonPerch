// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/render.hpp"

#include <chrono>
#include <cstdint>

struct wl_callback;
struct wl_surface;

namespace dp::wl {

class WaylandDisplay;

/// Paces the loop on `wl_surface.frame`, which is the compositor saying it is ready.
///
/// This is the only correct clock on Wayland, and it brings a large free win with it: a
/// surface the compositor is not showing -- another window covering it, a different virtual
/// desktop, the screen blanked -- simply stops being sent callbacks, and the pets stop
/// costing anything at all. No timer, no occlusion query and no visibility API needed.
///
/// The trap is that a callback has to be requested *before* the commit that will satisfy
/// it, and exactly once per frame. Requesting two leaves one unanswered for ever.
class FrameClock final : public IFrameClock {
public:
    FrameClock(WaylandDisplay& display, wl_surface* surface);
    ~FrameClock() override;

    std::chrono::nanoseconds wait_for_next_frame() override;

    /// True once the compositor has gone away, which is how the loop learns to stop.
    [[nodiscard]] bool disconnected() const noexcept { return disconnected_; }

private:
    static void done(void* data, wl_callback* callback, std::uint32_t time);

    WaylandDisplay* display_;
    wl_surface* surface_;
    wl_callback* pending_ = nullptr;
    bool arrived_ = false;
    bool disconnected_ = false;

    std::chrono::steady_clock::time_point previous_{};
    bool have_previous_ = false;
};

} // namespace dp::wl
