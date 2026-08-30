// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_script.hpp"

#include "dragonperch/text.hpp"
#include "errno_text.hpp"
#include "log.hpp"
#include "paths.hpp"

#include <cstdlib>
#include <string>
#include <system_error>

#include <systemd/sd-bus.h>

namespace dp::wl {
namespace {

constexpr const char* plugin_name = "dragonperch-geometry";
constexpr const char* relative = "kwin/scripts/dragonperch-geometry/contents/code/main.js";

std::filesystem::path data_home()
{
    // Read once, behind an initialiser the standard already guarantees is thread safe,
    // rather than on every call. There is no thread-safe getenv -- the check is right and has
    // no replacement to offer, so the honest answer is to narrow the window instead of
    // pretending it is not there. Nothing in this process writes the environment, and this
    // does run with the session bus worker alive: ask_kwin_for_a_report() runs from the
    // line just after SessionBus::start().
    static const std::filesystem::path cached = []() -> std::filesystem::path {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (const char* set = std::getenv("XDG_DATA_HOME"); set != nullptr && *set != '\0') {
            return set;
        }
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (const char* dir = std::getenv("HOME"); dir != nullptr && *dir != '\0') {
            return std::filesystem::path{dir} / ".local/share";
        }
        return {};
    }();
    return cached;
}

/// One call on KWin's scripting interface. Everything is a fire-and-forget request whose
/// only interesting outcome is whether it worked.
bool call(sd_bus* bus, const char* method, const char* signature, const char* first,
          const char* second)
{
    // Not SD_BUS_ERROR_NULL: that macro is a C99 compound literal, which C++ does not
    // have. Value-initialising means the same thing -- both fields null, the free flag
    // clear -- and does not need an extension.
    sd_bus_error error{};
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
        log_line(cat("kwin: ", method, " failed: ",
                     error.message != nullptr ? error.message : errno_text(-failed)));
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

    std::filesystem::path found;
    for (const std::filesystem::path& candidate : {
             executable_directory() / ".." / "share" / relative,
             data_home() / relative,
             std::filesystem::path{"/usr/share"} / relative,
             std::filesystem::path{"/usr/local/share"} / relative,
         }) {
        if (candidate.empty() || !std::filesystem::exists(candidate, failed)) {
            continue;
        }

        if (found.empty()) {
            found = std::filesystem::weakly_canonical(candidate, failed);
            continue;
        }

        // A copy in the user's data directory shadows the packaged one, which is right for
        // developing and wrong after an upgrade: `kwin/install.sh` run once from a checkout
        // leaves a script behind that a later package will not replace. Silently loading the
        // older of the two is the sort of thing that costs an evening, so say it.
        log_line(cat("kwin: ", found.string(), " is being used, and ", candidate.string(),
                     " is being shadowed. Delete the first if it is stale."));
        break;
    }

    if (!found.empty()) {
        return found;
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
