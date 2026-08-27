// SPDX-License-Identifier: GPL-3.0-or-later
#include "wayland_display.hpp"

#include "log.hpp"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <wayland-client.h>

namespace dp::wl {
namespace {

/// The most of each global we know how to talk to.
///
/// Binding a higher version than the generated header describes is a protocol error and
/// gets the client disconnected, so every bind is clamped. Binding *lower* than offered is
/// always allowed, which is what lets a client built against current protocol headers keep
/// running on an older compositor.
constexpr std::uint32_t compositor_version = 4;
constexpr std::uint32_t layer_shell_version = 4;
constexpr std::uint32_t output_version = 4;   ///< 4 is where wl_output.name arrives

} // namespace

WaylandDisplay::WaylandDisplay() = default;

WaylandDisplay::~WaylandDisplay()
{
    for (const std::unique_ptr<Output>& monitor : monitors_) {
        if (monitor->handle != nullptr) {
            wl_output_destroy(monitor->handle);
        }
    }
    if (layer_shell_ != nullptr) {
        zwlr_layer_shell_v1_destroy(layer_shell_);
    }
    if (compositor_ != nullptr) {
        wl_compositor_destroy(compositor_);
    }
    if (registry_ != nullptr) {
        wl_registry_destroy(registry_);
    }
    if (display_ != nullptr) {
        wl_display_disconnect(display_);
    }
}

void WaylandDisplay::connect()
{
    display_ = wl_display_connect(nullptr);
    if (display_ == nullptr) {
        throw std::runtime_error(
            "no Wayland display -- WAYLAND_DISPLAY is unset, or this is an X11 session");
    }

    static const wl_registry_listener listener = {
        .global = &WaylandDisplay::registry_global,
        .global_remove = &WaylandDisplay::registry_global_remove,
    };

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &listener, this);

    // Two round trips, and both are needed. The first delivers the globals; the second
    // delivers the events of the objects the first one made us bind -- an output describes
    // itself only after it exists.
    roundtrip();
    roundtrip();

    if (compositor_ == nullptr) {
        throw std::runtime_error("the compositor offers no wl_compositor, which cannot happen");
    }
    if (layer_shell_ == nullptr) {
        throw std::runtime_error(
            "no zwlr_layer_shell_v1. Plasma and the wlroots compositors have it; GNOME does "
            "not, and there is no other way for a normal client to place a surface above "
            "everything else");
    }

    rebuild_outputs();
    log_line(std::format("wayland: {} output(s), layer shell present", outputs_.size()));
    for (const OutputInfo& output : outputs_) {
        log_line(std::format("  {:<12} {}x{} at {},{} scale {:g}", output.name,
                             output.bounds.width, output.bounds.height, output.bounds.x,
                             output.bounds.y, output.scale));
    }
}

void WaylandDisplay::registry_global(void* data, wl_registry* registry, std::uint32_t name,
                                     const char* interface, std::uint32_t version)
{
    auto* self = static_cast<WaylandDisplay*>(data);
    const std::string_view which{interface};

    if (which == wl_compositor_interface.name) {
        self->compositor_ = static_cast<wl_compositor*>(wl_registry_bind(
            registry, name, &wl_compositor_interface, std::min(version, compositor_version)));
        return;
    }

    if (which == zwlr_layer_shell_v1_interface.name) {
        self->layer_shell_ = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface,
                             std::min(version, layer_shell_version)));
        return;
    }

    if (which == wl_output_interface.name) {
        static const wl_output_listener listener = {
            .geometry = &WaylandDisplay::output_geometry,
            .mode = &WaylandDisplay::output_mode,
            .done = &WaylandDisplay::output_done,
            .scale = &WaylandDisplay::output_scale,
            .name = &WaylandDisplay::output_name,
            .description = &WaylandDisplay::output_description,
        };

        auto monitor = std::make_unique<Output>();
        monitor->name = name;
        monitor->handle = static_cast<wl_output*>(wl_registry_bind(
            registry, name, &wl_output_interface, std::min(version, output_version)));
        monitor->label = std::format("output-{}", name);

        wl_output_add_listener(monitor->handle, &listener, self);
        self->monitors_.push_back(std::move(monitor));
    }
}

void WaylandDisplay::registry_global_remove(void* data, wl_registry*, std::uint32_t name)
{
    auto* self = static_cast<WaylandDisplay*>(data);

    const auto gone = std::ranges::find(self->monitors_, name, [](const auto& monitor) {
        return monitor->name;
    });
    if (gone == self->monitors_.end()) {
        return;
    }

    log_line(std::format("wayland: output {} unplugged", (*gone)->label));
    wl_output_destroy((*gone)->handle);
    self->monitors_.erase(gone);
    self->rebuild_outputs();
}

void WaylandDisplay::output_geometry(void* data, wl_output* output, int x, int y, int, int, int,
                                     const char*, const char*, int)
{
    auto* self = static_cast<WaylandDisplay*>(data);
    if (Output* monitor = self->find(output); monitor != nullptr) {
        monitor->x = x;
        monitor->y = y;
    }
}

void WaylandDisplay::output_mode(void* data, wl_output* output, std::uint32_t flags, int width,
                                 int height, int)
{
    // Only the current mode. An output advertises every mode it supports, and taking the
    // last one seen would size the overlay to whatever the monitor happens to list last.
    if ((flags & WL_OUTPUT_MODE_CURRENT) == 0) {
        return;
    }

    auto* self = static_cast<WaylandDisplay*>(data);
    if (Output* monitor = self->find(output); monitor != nullptr) {
        monitor->width = width;
        monitor->height = height;
    }
}

void WaylandDisplay::output_scale(void* data, wl_output* output, int factor)
{
    auto* self = static_cast<WaylandDisplay*>(data);
    if (Output* monitor = self->find(output); monitor != nullptr) {
        monitor->scale = std::max(1, factor);
    }
}

void WaylandDisplay::output_name(void* data, wl_output* output, const char* name)
{
    auto* self = static_cast<WaylandDisplay*>(data);
    if (Output* monitor = self->find(output); monitor != nullptr && name != nullptr) {
        // The connector name, "DP-1" or "eDP-1". This is also what the KWin script reports
        // a window's screen as, so the two sides can be matched up by it.
        monitor->label = name;
    }
}

void WaylandDisplay::output_description(void*, wl_output*, const char*)
{
    // Human-readable make and model. Nothing here needs it.
}

void WaylandDisplay::output_done(void* data, wl_output* output)
{
    auto* self = static_cast<WaylandDisplay*>(data);
    if (Output* monitor = self->find(output); monitor != nullptr) {
        monitor->described = true;
    }
}

WaylandDisplay::Output* WaylandDisplay::find(wl_output* handle)
{
    const auto it = std::ranges::find(monitors_, handle, [](const auto& monitor) {
        return monitor->handle;
    });
    return it == monitors_.end() ? nullptr : it->get();
}

void WaylandDisplay::rebuild_outputs()
{
    outputs_.clear();

    for (const std::unique_ptr<Output>& monitor : monitors_) {
        if (!monitor->described || monitor->width <= 0 || monitor->height <= 0) {
            continue;
        }

        // Logical units. wl_output reports its position in them and its mode in physical
        // pixels, and mixing the two is the classic Wayland arithmetic bug: on a 2x display
        // it makes every monitor after the first sit twice as far out as it really is.
        const PixelRect bounds{monitor->x, monitor->y, monitor->width / monitor->scale,
                               monitor->height / monitor->scale};

        outputs_.push_back(OutputInfo{
            .id = static_cast<std::int64_t>(monitor->name),
            .bounds = bounds,

            // A Wayland client is not told where the panels are; that is exactly the
            // knowledge the protocol withholds. The geometry provider fills this in
            // properly, and until it has, the whole output is fair game.
            .work_area = bounds,
            .scale = static_cast<double>(monitor->scale),
            .name = monitor->label,
        });
    }

    std::ranges::sort(outputs_, {}, [](const OutputInfo& output) {
        return std::pair{output.bounds.x, output.bounds.y};
    });
}

wl_output* WaylandDisplay::output_handle(std::int64_t id) const
{
    const auto it = std::ranges::find(monitors_, static_cast<std::uint32_t>(id),
                                      [](const auto& monitor) { return monitor->name; });
    return it == monitors_.end() ? nullptr : (*it)->handle;
}

void WaylandDisplay::roundtrip()
{
    if (wl_display_roundtrip(display_) < 0) {
        throw std::runtime_error("the Wayland connection dropped");
    }
}

bool WaylandDisplay::dispatch()
{
    return wl_display_dispatch(display_) >= 0;
}

bool WaylandDisplay::dispatch_pending()
{
    // Only what has already been queued. wl_display_dispatch would block, and this exists
    // for the caller that must not.
    if (wl_display_flush(display_) < 0) {
        return false;
    }
    return wl_display_dispatch_pending(display_) >= 0;
}

} // namespace dp::wl
