// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"

#include <chrono>
#include <cstddef>
#include <span>

namespace dp {

/// One sprite blit, in global physical pixels.
struct SpriteDraw {
    int atlas_id = 0;
    PixelRect source{};
    PixelPoint destination{};
    bool flip_x = false;
    float opacity = 1.0F;
};

/// The only drawing surface the simulation ever sees.
///
/// Deliberately tiny: alpha-blended axis-aligned blits from a texture atlas, nothing else.
/// That is the entire vocabulary this app needs, and keeping it this small is what makes
/// "Direct2D on Windows, OpenGL on Wayland" cost almost nothing. Resist adding vector
/// paths here.
class ISpriteRenderer {
public:
    ISpriteRenderer() = default;
    ISpriteRenderer(const ISpriteRenderer&) = delete;
    ISpriteRenderer& operator=(const ISpriteRenderer&) = delete;
    ISpriteRenderer(ISpriteRenderer&&) = delete;
    ISpriteRenderer& operator=(ISpriteRenderer&&) = delete;
    virtual ~ISpriteRenderer() = default;

    /// Premultiplied BGRA, top-down, stride = `size.width * 4`. Returns an atlas id.
    virtual int register_atlas(std::span<const std::byte> premultiplied_bgra, PixelSize size) = 0;

    virtual void begin_frame() = 0;
    virtual void draw(const SpriteDraw& sprite) = 0;
    virtual void end_frame() = 0;
};

/// Paces the simulation. Backends drive this from the compositor, never from a timer.
///
/// Wayland: `wl_surface.frame`. Not a nicety -- it is the only correct way to schedule
/// drawing, and it brings a large free win, because an occluded surface simply stops
/// receiving callbacks and the pets pause at no cost. Windows: `DwmFlush`, which blocks
/// until DWM finishes its next present and so self-throttles to the refresh rate.
class IFrameClock {
public:
    IFrameClock() = default;
    IFrameClock(const IFrameClock&) = delete;
    IFrameClock& operator=(const IFrameClock&) = delete;
    IFrameClock(IFrameClock&&) = delete;
    IFrameClock& operator=(IFrameClock&&) = delete;
    virtual ~IFrameClock() = default;

    /// Blocks until the compositor is ready, and returns the time since the previous frame.
    virtual std::chrono::nanoseconds wait_for_next_frame() = 0;
};

} // namespace dp
