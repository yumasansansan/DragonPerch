// SPDX-License-Identifier: GPL-3.0-or-later
#include "layer_surface.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"
#include "wayland_display.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <stdexcept>
#include <utility>

#include <GLES3/gl3.h>
#include <wayland-client.h>
#include <wayland-egl.h>

namespace dp::wl {

LayerSurface::~LayerSurface()
{
    release();
}

LayerSurface::LayerSurface(LayerSurface&& other) noexcept
{
    *this = std::move(other);
}

LayerSurface& LayerSurface::operator=(LayerSurface&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    release();

    egl_ = std::exchange(other.egl_, nullptr);
    surface_ = std::exchange(other.surface_, nullptr);
    layer_ = std::exchange(other.layer_, nullptr);
    window_ = std::exchange(other.window_, nullptr);
    egl_surface_ = std::exchange(other.egl_surface_, EGL_NO_SURFACE);
    bounds_ = other.bounds_;
    scale_ = other.scale_;
    configured_ = other.configured_;
    closed_ = other.closed_;

    // The compositor calls back with `this` as the user pointer, so a moved surface has to
    // re-point its listener or the events land on a dead object.
    if (layer_ != nullptr) {
        zwlr_layer_surface_v1_set_user_data(layer_, this);
    }
    return *this;
}

void LayerSurface::release() noexcept
{
    if (egl_ != nullptr) {
        egl_->destroy_surface(egl_surface_);
    }
    egl_surface_ = EGL_NO_SURFACE;

    if (window_ != nullptr) {
        wl_egl_window_destroy(window_);
        window_ = nullptr;
    }
    if (layer_ != nullptr) {
        zwlr_layer_surface_v1_destroy(layer_);
        layer_ = nullptr;
    }
    if (surface_ != nullptr) {
        wl_surface_destroy(surface_);
        surface_ = nullptr;
    }
}

void LayerSurface::create(WaylandDisplay& display, EglContext& egl, const OutputInfo& output)
{
    egl_ = &egl;
    bounds_ = output.bounds;
    scale_ = static_cast<int>(output.scale);

    surface_ = wl_compositor_create_surface(display.compositor());
    if (surface_ == nullptr) {
        throw std::runtime_error("wl_compositor.create_surface returned nothing");
    }

    layer_ = zwlr_layer_shell_v1_get_layer_surface(display.layer_shell(), surface_,
                                                   display.output_handle(output.id),
                                                   ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                                                   "dragonperch");
    if (layer_ == nullptr) {
        throw std::runtime_error("zwlr_layer_shell_v1.get_layer_surface returned nothing");
    }

    static const zwlr_layer_surface_v1_listener listener = {
        .configure = &LayerSurface::configure,
        .closed = &LayerSurface::closed_callback,
    };
    zwlr_layer_surface_v1_add_listener(layer_, &listener, this);

    // Anchored to all four edges with a size of 0x0, which is how layer shell spells "the
    // whole output" -- the compositor fills the size in on the configure event, and it
    // stays right when the monitor's mode changes underneath us.
    zwlr_layer_surface_v1_set_anchor(layer_,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                                         | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                                         | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
                                         | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(layer_, 0, 0);

    // -1, not 0. Zero means "the compositor may push panels aside for me"; -1 means this
    // surface is ignored when the usable area is worked out. A desktop pet that made the
    // taskbar move over would be a poor guest.
    zwlr_layer_surface_v1_set_exclusive_zone(layer_, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer_, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    // Click-through. An empty region -- not a null one: null means "the whole surface",
    // which is the opposite, and is the default.
    wl_region* nothing = wl_compositor_create_region(display.compositor());
    wl_surface_set_input_region(surface_, nothing);
    wl_region_destroy(nothing);

    wl_surface_set_buffer_scale(surface_, scale_);

    // A commit with no buffer attached is what asks for the first configure. Attaching
    // one before the configure has been acked is a protocol error.
    wl_surface_commit(surface_);
    display.roundtrip();

    if (!configured_) {
        throw std::runtime_error("the layer surface was never configured");
    }

    const PixelSize buffer = buffer_size();
    window_ = wl_egl_window_create(surface_, buffer.width, buffer.height);
    if (window_ == nullptr) {
        throw std::runtime_error("wl_egl_window_create returned nothing");
    }

    egl_surface_ = egl.create_surface(window_);

    // One transparent frame, immediately. Until a buffer is attached the surface is not
    // mapped, and an unmapped surface is never presented -- so the compositor never
    // answers wl_surface.frame, and the render loop's first wait never returns. Nothing
    // appears, nothing errors, and the process sits there looking alive.
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    egl.swap(egl_surface_);

    log_line(cat("layer surface on ", output.name, ": ", bounds_.width, "x", bounds_.height,
                 " logical, ", buffer.width, "x", buffer.height, " buffer"));
}

void LayerSurface::configure(void* data, zwlr_layer_surface_v1* layer, std::uint32_t serial,
                             std::uint32_t width, std::uint32_t height)
{
    auto* self = static_cast<LayerSurface*>(data);

    // Ack first, always. The compositor is waiting for it, and a surface that never acks
    // simply never appears -- with no error anywhere to say why.
    zwlr_layer_surface_v1_ack_configure(layer, serial);

    if (width > 0 && height > 0) {
        self->bounds_.width = static_cast<int>(width);
        self->bounds_.height = static_cast<int>(height);
    }
    self->configured_ = true;

    if (self->window_ != nullptr) {
        const PixelSize buffer = self->buffer_size();
        wl_egl_window_resize(self->window_, buffer.width, buffer.height, 0, 0);
    }
}

void LayerSurface::closed_callback(void* data, zwlr_layer_surface_v1*)
{
    // The output went away, or the compositor is shutting down. Not an error.
    static_cast<LayerSurface*>(data)->closed_ = true;
}

PixelSize LayerSurface::buffer_size() const noexcept
{
    return PixelSize{bounds_.width * scale_, bounds_.height * scale_};
}

} // namespace dp::wl
