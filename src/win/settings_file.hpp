// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/settings.hpp"

#include <filesystem>

namespace dp::win {

/// %APPDATA%\\DragonPerch\\dragonperchrc.
///
/// Where the file lives is the one platform-specific thing about settings, which is why it
/// is here and the reading and writing are in the core. No extension, matching the name
/// KConfig would give it on the other platform: one file format, one file name, two places.
[[nodiscard]] std::filesystem::path settings_path();

/// Reads it, or the defaults if there is nothing to read.
[[nodiscard]] Settings load_settings();

} // namespace dp::win
