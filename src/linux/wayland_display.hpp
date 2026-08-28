// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/world.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct wl_compositor;
struct wl_display;
struct wl_output;
struct wl_registry;
struct zwlr_layer_shell_v1;

namespace dp::wl {

/// The connection, the globals we need from it, and the monitors.
///
/// Everything here is a pure Wayland client concern. What the *other* windows on the
/// desktop are doing is not knowable from this side at all -- see kwin_geometry.hpp.
class WaylandDisplay {
public:
    WaylandDisplay();
    ~WaylandDisplay();

    WaylandDisplay(const WaylandDisplay&) = delete;
    WaylandDisplay& operator=(const WaylandDisplay&) = delete;
    WaylandDisplay(WaylandDisplay&&) = delete;
    WaylandDisplay& operator=(WaylandDisplay&&) = delete;

    /// Connects, binds the globals and waits for the outputs to finish describing
    /// themselves. Throws if there is no display, or no layer shell on it.
    void connect();

    [[nodiscard]] wl_display* display() const noexcept { return display_; }
    [[nodiscard]] wl_compositor* compositor() const noexcept { return compositor_; }
    [[nodiscard]] zwlr_layer_shell_v1* layer_shell() const noexcept { return layer_shell_; }

    /// One entry per monitor, in the core's own terms.
    ///
    /// Coordinates are *logical*, not physical: that is what a Wayland client is given, it
    /// is what the KWin script reports for other windows, and the two have to agree or the
    /// pets stand next to title bars rather than on them. On a 2x display a 3840-pixel
    /// monitor is 1920 units wide here, and the renderer scales at the very end.
    [[nodiscard]] std::span<const OutputInfo> outputs() const noexcept { return outputs_; }

    /// The `wl_output` an OutputInfo came from, for binding a layer surface to it.
    [[nodiscard]] wl_output* output_handle(std::int64_t id) const;

    /// Sends everything queued and waits for the replies. Throws if the connection dropped.
    void roundtrip();

    enum class Dispatch {
        ok,
        /// A signal arrived while blocked. Not an error, and not something to retry
        /// blindly: it is the only chance the caller gets to notice it was asked to stop.
        interrupted,
        /// The compositor has gone away.
        failed,
    };

    /// Reads whatever has arrived, blocking until at least one event does.
    [[nodiscard]] Dispatch dispatch();

private:
    /// A monitor, as it is described to us across several events before `done`.
    struct Output {
        wl_output* handle = nullptr;
        std::uint32_t name = 0;   ///< the registry name, which is what removal quotes

        int x = 0;
        int y = 0;
        int width = 0;            ///< physical pixels, straight off the mode event
        int height = 0;
        int scale = 1;
        std::string label;
        bool described = false;
    };

    static void registry_global(void* data, wl_registry* registry, std::uint32_t name,
                                const char* interface, std::uint32_t version);
    static void registry_global_remove(void* data, wl_registry* registry, std::uint32_t name);

    static void output_geometry(void* data, wl_output* output, int x, int y, int physical_width,
                                int physical_height, int subpixel, const char* make,
                                const char* model, int transform);
    static void output_mode(void* data, wl_output* output, std::uint32_t flags, int width,
                            int height, int refresh);
    static void output_done(void* data, wl_output* output);
    static void output_scale(void* data, wl_output* output, int factor);
    static void output_name(void* data, wl_output* output, const char* name);
    static void output_description(void* data, wl_output* output, const char* description);

    Output* find(wl_output* handle);
    void rebuild_outputs();

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    zwlr_layer_shell_v1* layer_shell_ = nullptr;

    std::vector<std::unique_ptr<Output>> monitors_;
    std::vector<OutputInfo> outputs_;
};

} // namespace dp::wl
