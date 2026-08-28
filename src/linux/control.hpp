// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string_view>

struct sd_bus_message;
struct sd_bus_error;

namespace dp::wl {

class SessionBus;

/// What one DragonPerch can be asked to do by another, or by the tray.
///
/// The same four the Windows head answers over WM_COPYDATA. One vocabulary, so that the
/// tray, the settings program and the command line are three callers of one thing rather
/// than three mechanisms that drift.
enum class Command {
    quit,
    pause,
    resume,
    toggle_pause,
    reload,
};

[[nodiscard]] std::string_view name_of(Command command) noexcept;

/// `org.dragonperch.Control1` at `/org/dragonperch/Control`, on the `org.dragonperch`
/// name.
///
/// Published on the connection the geometry reports already arrive on: both are D-Bus,
/// neither is busy, and a second connection with a second thread would buy nothing.
///
/// The handler runs on the bus thread, not the render loop, so what it touches has to be
/// safe from there -- which is why PetHost's pause flag is atomic.
class ControlService {
public:
    using Handler = std::function<void(Command)>;

    /// Registers the object. The service must outlive the bus.
    void publish(SessionBus& bus, Handler handler);

private:
    static int on_call(sd_bus_message* message, void* userdata, sd_bus_error* error);

    Handler handler_;
};

/// The sending end: calls the method on whichever DragonPerch owns the name.
///
/// Opens a connection of its own, because this runs in a *different process* -- and even
/// in the same one, a blocking call on the connection the bus thread is processing would
/// be a race. False when nobody is listening.
[[nodiscard]] bool send_command(Command command);

} // namespace dp::wl
