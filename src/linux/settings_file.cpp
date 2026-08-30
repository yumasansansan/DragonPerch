// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_file.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"

#include <cstdlib>
#include <fstream>
#include <ios>
#include <string>

namespace dp::wl {
namespace {

std::filesystem::path config_home()
{
    // Read once, behind an initialiser the standard already guarantees is thread safe,
    // rather than on every call. There is no thread-safe getenv -- the check is right and has
    // no replacement to offer, so the honest answer is to narrow the window instead of
    // pretending it is not there. Nothing in this process writes the environment, and this
    // does run with the session bus worker alive: the settings are re-read on the fly
    // whenever the file changes.
    static const std::filesystem::path cached = []() -> std::filesystem::path {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (const char* set = std::getenv("XDG_CONFIG_HOME"); set != nullptr && *set != 0) {
            return set;
        }
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (const char* dir = std::getenv("HOME"); dir != nullptr && *dir != 0) {
            return std::filesystem::path{dir} / ".config";
        }
        return {};
    }();
    return cached;
}

} // namespace

std::filesystem::path settings_path()
{
    const std::filesystem::path base = config_home();
    if (base.empty()) {
        return {};
    }
    return base / "dragonperch" / "dragonperchrc";
}

Settings load_settings()
{
    const std::filesystem::path path = settings_path();
    if (path.empty()) {
        return {};
    }

    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        // Nothing saved yet, which is the ordinary case the first time.
        return {};
    }

    const std::streamoff size = stream.tellg();
    if (size < 0) {
        return {};
    }

    std::string text(static_cast<std::size_t>(size), 0);
    stream.seekg(0);
    stream.read(text.data(), size);
    text.resize(static_cast<std::size_t>(stream.gcount()));

    log_line(cat("settings: ", path.string()));
    return parse_settings(text);
}

} // namespace dp::wl
