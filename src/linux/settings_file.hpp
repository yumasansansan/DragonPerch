// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/settings.hpp"

#include <filesystem>

namespace dp::wl {

/// $XDG_CONFIG_HOME/dragonperch/dragonperchrc, or ~/.config below it.
///
/// The name is KConfig's convention, because the settings program is a KDE Config Module
/// and KConfig is what will be writing this file. Where it lives is the one
/// platform-specific thing about settings; the reading and writing are in the core.
[[nodiscard]] std::filesystem::path settings_path();

/// Reads it, or the defaults if there is nothing to read.
[[nodiscard]] Settings load_settings();

} // namespace dp::wl
