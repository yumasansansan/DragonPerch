// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/render.hpp"
#include "dragonperch/world.hpp"
#include "gpu_device.hpp"
#include "output_surface.hpp"

#include <span>
#include <vector>

namespace dp::win {

/// Draws the simulation's sprites through DirectComposition, one overlay per monitor.
class SpriteRenderer final : public ISpriteRenderer {
public:
    SpriteRenderer();

    /// Rebuilds one overlay per output. Each is inset by a pixel: a topmost borderless
    /// window that matches a monitor exactly makes Windows enable Do Not Disturb.
    void set_outputs(std::span<const OutputInfo> outputs);

    int register_atlas(std::span<const std::byte> premultiplied_bgra, PixelSize size) override;

    void begin_frame() override;
    void draw(const SpriteDraw& sprite) override;
    void end_frame() override;

    [[nodiscard]] GpuDevice& device() noexcept { return device_; }

private:
    /// One pixel shorter than the monitor. See set_outputs.
    static constexpr int fullscreen_inset = 1;

    GpuDevice device_;
    std::vector<OutputSurface> surfaces_;
    std::vector<ComPtr<ID2D1Bitmap1>> atlases_;

    std::vector<SpriteDraw> pending_;

    /// What was drawn last frame. The damage region is the union of that and this frame's,
    /// so a sprite that moved is erased from where it was.
    PixelRect previous_damage_{};
};

} // namespace dp::win
