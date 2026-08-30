// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"
#include "gpu_device.hpp"
#include "overlay_window.hpp"
#include "win_headers.hpp"

#include <functional>

namespace dp::win {

/// One monitor's overlay: the host window, and the composition target, visual and surface
/// bound to it.
class OutputSurface {
public:
    /// `bounds` must be strictly smaller than the monitor. A topmost borderless window
    /// matching a monitor exactly makes Windows report QUNS_BUSY and turn on Do Not Disturb
    /// -- a size heuristic, nothing to do with transparency or composition.
    static OutputSurface create(GpuDevice& device, const PixelRect& bounds);

    OutputSurface(const OutputSurface&) = delete;
    OutputSurface& operator=(const OutputSurface&) = delete;
    OutputSurface(OutputSurface&&) noexcept = default;
    OutputSurface& operator=(OutputSurface&&) noexcept = default;
    ~OutputSurface() = default;

    [[nodiscard]] const PixelRect& bounds() const noexcept { return window_.bounds(); }

    void set_visible(bool visible) { window_.set_visible(visible); }
    [[nodiscard]] bool visible() const noexcept { return window_.visible(); }

    /// Says the next draw has to cover the whole surface rather than only what it is
    /// given, and reports whether one is owed.
    ///
    /// Two things ask for this. A surface that has never been written has undefined
    /// contents. So does one whose contents have stopped being maintained -- a hidden
    /// overlay is not drawn at all, so what was on it when it went away is still there when
    /// it comes back, and a caller tracking damage has long since forgotten those pixels
    /// existed.
    void invalidate() noexcept { needs_full_draw_ = true; }
    [[nodiscard]] bool needs_full_draw() const noexcept { return needs_full_draw_; }

    /// Opens a drawing session over `dirty` (in global coordinates), hands the Direct2D
    /// context to `draw`, then closes it. The caller commits once for all surfaces.
    ///
    /// IDCompositionSurface::BeginDraw takes the dirty rectangle, so damage tracking is
    /// native here: only that region is uploaded, nothing is read back, and the atlas stays
    /// a GPU texture.
    void draw(const PixelRect& dirty, const std::function<void(ID2D1DeviceContext*)>& body);

private:
    OutputSurface(GpuDevice& device, OverlayWindow window, ComPtr<IDCompositionTarget> target,
                  ComPtr<IDCompositionVisual2> visual, ComPtr<IDCompositionSurface> surface);

    GpuDevice* device_ = nullptr;
    OverlayWindow window_;
    ComPtr<IDCompositionTarget> target_;
    ComPtr<IDCompositionVisual2> visual_;
    ComPtr<IDCompositionSurface> surface_;

    /// See invalidate(). True until the whole surface has been written at least once, and
    /// again whenever somebody says the contents can no longer be relied on.
    bool needs_full_draw_ = true;
};

} // namespace dp::win
