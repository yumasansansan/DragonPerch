// SPDX-License-Identifier: GPL-3.0-or-later
#include "control.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"
#include "session_bus.hpp"

#include <array>
#include <string>
#include <cstring>
#include <utility>

#include <systemd/sd-bus.h>

namespace dp::wl {
namespace {

constexpr const char* bus_name = "org.dragonperch";
constexpr const char* object_path = "/org/dragonperch/Control";
constexpr const char* interface_name = "org.dragonperch.Control1";

constexpr std::array<std::pair<Command, std::string_view>, 5> commands{{
    {Command::quit, "Quit"},
    {Command::pause, "Pause"},
    {Command::resume, "Resume"},
    {Command::toggle_pause, "TogglePause"},
    {Command::reload, "Reload"},
}};

} // namespace

std::string_view name_of(Command command) noexcept
{
    for (const auto& [value, name] : commands) {
        if (value == command) {
            return name;
        }
    }
    return "?";
}

int ControlService::on_call(sd_bus_message* message, void* userdata, sd_bus_error*)
{
    auto* self = static_cast<ControlService*>(userdata);

    // Which method was called, rather than five near-identical callbacks. The member name
    // is what the vtable matched on, so this cannot disagree with the table.
    const char* member = sd_bus_message_get_member(message);
    if (member != nullptr && self->handler_) {
        for (const auto& [command, name] : commands) {
            if (name == member) {
                log_line(cat("control: ", name));
                self->handler_(command);
                break;
            }
        }
    }

    return sd_bus_reply_method_return(message, "");
}

void ControlService::publish(SessionBus& bus, Handler handler)
{
    handler_ = std::move(handler);

    // Declared here so it can name a private member, and static so it outlives the call --
    // sd-bus reads the table for as long as the object is published.
    static const sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Quit", "", "", &ControlService::on_call, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("Pause", "", "", &ControlService::on_call, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("Resume", "", "", &ControlService::on_call, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("TogglePause", "", "", &ControlService::on_call, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("Reload", "", "", &ControlService::on_call, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END,
    };

    bus.add_object(object_path, interface_name, vtable, this);
}

bool send_command(Command command)
{
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) {
        return false;
    }

    // Not SD_BUS_ERROR_NULL: that macro is a C99 compound literal, which C++ does not have.
    sd_bus_error error{};
    sd_bus_message* reply = nullptr;

    const std::string method{name_of(command)};
    const int failed = sd_bus_call_method(bus, bus_name, object_path, interface_name,
                                          method.c_str(), &error, &reply, "");
    if (failed < 0 && error.message != nullptr) {
        log_line(cat("control: ", error.message));
    }

    sd_bus_error_free(&error);
    if (reply != nullptr) {
        sd_bus_message_unref(reply);
    }
    sd_bus_flush_close_unref(bus);

    return failed >= 0;
}

} // namespace dp::wl
