// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"
#include "dragonperch/world.hpp"
#include "egl_context.hpp"

#include <cstdint>

struct wl_egl_window;
struct wl_surface;
struct zwlr_layer_surface_v1;

namespace dp::wl {

class WaylandDisplay;

/// One full-output overlay: a layer-shell surface on the overlay layer, with no input
/// region at all.
///
/// The empty input region is the whole click-through story on Wayland, and it is a good
/// deal simpler than the Windows one -- there is no equivalent of hunting for the
/// combination of extended window styles that neither swallows clicks nor turns on Do Not
/// Disturb. `wl_surface.set_input_region` with an empty region means the compositor never
/// considers this surface for pointer or touch input, full stop.
class LayerSurface {
public:
    LayerSurface() = default;
    ~LayerSurface();

    LayerSurface(const LayerSurface&) = delete;
    LayerSurface& operator=(const LayerSurface&) = delete;
    LayerSurface(LayerSurface&& other) noexcept;
    LayerSurface& operator=(LayerSurface&& other) noexcept;

    /// Creates the surface on `output` and waits for the compositor to configure it.
    void create(WaylandDisplay& display, EglContext& egl, const OutputInfo& output);

    [[nodiscard]] wl_surface* surface() const noexcept { return surface_; }
    [[nodiscard]] EGLSurface egl_surface() const noexcept { return egl_surface_; }

    /// The output this covers, in the shared desktop space.
    [[nodiscard]] const PixelRect& bounds() const noexcept { return bounds_; }

    /// Buffer size in real pixels: the logical size times the output scale.
    [[nodiscard]] PixelSize buffer_size() const noexcept;

    [[nodiscard]] int scale() const noexcept { return scale_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }

private:
    static void configure(void* data, zwlr_layer_surface_v1* layer, std::uint32_t serial,
                          std::uint32_t width, std::uint32_t height);
    static void closed_callback(void* data, zwlr_layer_surface_v1* layer);

    void release() noexcept;

    EglContext* egl_ = nullptr;
    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_ = nullptr;
    wl_egl_window* window_ = nullptr;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;

    PixelRect bounds_{};
    int scale_ = 1;
    bool configured_ = false;
    bool closed_ = false;
};

} // namespace dp::wl
