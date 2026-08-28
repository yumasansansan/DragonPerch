// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_file.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"
#include "win_headers.hpp"

#include <fstream>
#include <ios>
#include <string>

#include <shlobj.h>

namespace dp::win {
namespace {

std::filesystem::path roaming_appdata()
{
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        CoTaskMemFree(raw);
        return {};
    }

    std::filesystem::path path{raw};
    CoTaskMemFree(raw);
    return path;
}

} // namespace

std::filesystem::path settings_path()
{
    const std::filesystem::path base = roaming_appdata();
    if (base.empty()) {
        return {};
    }
    return base / "DragonPerch" / "dragonperchrc";
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

} // namespace dp::win
