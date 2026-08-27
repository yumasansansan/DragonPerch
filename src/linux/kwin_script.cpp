// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_script.hpp"

#include "log.hpp"

#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <system_error>

#include <systemd/sd-bus.h>

namespace dp::wl {
namespace {

constexpr const char* plugin_name = "dragonperch-geometry";
constexpr const char* relative = "kwin/scripts/dragonperch-geometry/contents/code/main.js";

std::filesystem::path data_home()
{
    if (const char* set = std::getenv("XDG_DATA_HOME"); set != nullptr && *set != '\0') {
        return set;
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path{home} / ".local/share";
    }
    return {};
}

std::filesystem::path executable_directory()
{
    std::error_code failed;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", failed);
    return failed ? std::filesystem::current_path() : self.parent_path();
}

/// One call on KWin's scripting interface. Everything is a fire-and-forget request whose
/// only interesting outcome is whether it worked.
bool call(sd_bus* bus, const char* method, const char* signature, const char* first,
          const char* second)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int failed = 0;
    if (signature == nullptr) {
        failed = sd_bus_call_method(bus, "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting",
                                    method, &error, &reply, "");
    } else if (second == nullptr) {
        failed = sd_bus_call_method(bus, "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting",
                                    method, &error, &reply, signature, first);
    } else {
        failed = sd_bus_call_method(bus, "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting",
                                    method, &error, &reply, signature, first, second);
    }

    if (failed < 0) {
        log_line(std::format("kwin: {} failed: {}", method,
                             error.message != nullptr ? error.message : std::strerror(-failed)));
    }

    sd_bus_error_free(&error);
    if (reply != nullptr) {
        sd_bus_message_unref(reply);
    }
    return failed >= 0;
}

} // namespace

std::filesystem::path find_kwin_script()
{
    std::error_code failed;

    for (const std::filesystem::path& candidate : {
             executable_directory() / ".." / "share" / relative,
             data_home() / relative,
             std::filesystem::path{"/usr/share"} / relative,
             std::filesystem::path{"/usr/local/share"} / relative,
         }) {
        if (!candidate.empty() && std::filesystem::exists(candidate, failed)) {
            return std::filesystem::weakly_canonical(candidate, failed);
        }
    }

    // The source tree, so it can be tried without installing anything.
    std::filesystem::path directory = executable_directory();
    for (int i = 0; i < 8; ++i) {
        const std::filesystem::path candidate =
            directory / "kwin/dragonperch-geometry/contents/code/main.js";
        if (std::filesystem::exists(candidate, failed)) {
            return std::filesystem::weakly_canonical(candidate, failed);
        }

        if (!directory.has_parent_path() || directory.parent_path() == directory) {
            break;
        }
        directory = directory.parent_path();
    }

    return {};
}

bool reload_kwin_script(const std::filesystem::path& script)
{
    if (script.empty()) {
        return false;
    }

    // A connection of its own, opened and closed around the three calls. The provider's bus
    // is being processed by its own thread, and a blocking call on it from here would race.
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) {
        return false;
    }

    const std::string path = script.string();

    // Unload first. loadScript on a name that is already loaded hands back the existing
    // instance without running it again, which is exactly the thing being asked for here.
    (void)call(bus, "unloadScript", "s", plugin_name, nullptr);

    const bool loaded = call(bus, "loadScript", "ss", path.c_str(), plugin_name);
    if (loaded) {
        // Nothing runs until start(), and start() runs everything loaded and not yet
        // started -- so this is also what makes the freshly loaded copy execute.
        (void)call(bus, "start", nullptr, nullptr, nullptr);
    }

    sd_bus_flush_close_unref(bus);
    return loaded;
}

} // namespace dp::wl
