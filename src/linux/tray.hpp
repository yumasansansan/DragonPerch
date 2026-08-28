// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "control.hpp"

#include <cstdint>
#include <functional>
#include <vector>

struct sd_bus;
struct sd_bus_message;
struct sd_bus_error;

namespace dp::wl {

class SessionBus;

/// The notification-area icon, through StatusNotifierItem.
///
/// There is no one call for this on Wayland. The XEmbed system tray does not exist here,
/// and what Plasma implements is StatusNotifierItem: the program owns a bus name, exports
/// an object describing the icon, exports a second object describing a menu, and asks
/// StatusNotifierWatcher to take notice. All D-Bus, so sd-bus can do it without a toolkit
/// -- four interfaces rather than one function, and this is the price of that.
///
/// The menu is *described*, not drawn. Plasma's own tray widget builds it from these
/// labels and toggle states, in Breeze, with the user's colour scheme, font, icon theme and
/// scale. That comes free and cannot drift, and is the strongest argument for the protocol.
class TrayIcon {
public:
    using Handler = std::function<void(Command)>;
    using PausedQuery = std::function<bool()>;

    /// Exports the objects. Call before the bus starts processing.
    void publish(SessionBus& bus, Handler handler, PausedQuery paused);

    /// Asks the watcher to take notice. Call after the bus is running -- it is a method
    /// call, so it needs a connection that is being processed. False when no watcher has
    /// registered, which is a session with no tray rather than an error.
    [[nodiscard]] bool register_with_watcher();

private:
    static int on_item_property(sd_bus* bus, const char* path, const char* interface,
                                const char* property, sd_bus_message* reply, void* userdata,
                                sd_bus_error* error);
    static int on_item_method(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int on_menu_layout(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int on_menu_event(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int on_menu_about_to_show(sd_bus_message* message, void* userdata, sd_bus_error* error);
    static int on_menu_group_properties(sd_bus_message* message, void* userdata,
                                        sd_bus_error* error);
    static int on_menu_property(sd_bus* bus, const char* path, const char* interface,
                                const char* property, sd_bus_message* reply, void* userdata,
                                sd_bus_error* error);

    /// The watcher appearing after we did. StatusNotifierWatcher is part of the shell, and
    /// a program started with the session can easily be ready first -- this is the same
    /// problem as Explorer restarting on the other platform, and needs the same answer.
    static int on_watcher_appeared(sd_bus_message* message, void* userdata, sd_bus_error* error);

    [[nodiscard]] bool paused() const;

    Handler handler_;
    PausedQuery paused_query_;

    /// The icon as ARGB32, big-endian, which is what StatusNotifierItem's `a(iiay)` wants.
    /// Kept because the property may be read more than once and re-decoding a PNG each time
    /// would be silly.
    std::vector<std::uint8_t> pixels_;
    int width_ = 0;
    int height_ = 0;

    /// dbusmenu makes hosts pass the revision back, and it only has to increase.
    std::uint32_t menu_revision_ = 1;
};

} // namespace dp::wl
