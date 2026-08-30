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

/// Writes it, creating the directory if it is not there. False if it could not be written.
///
/// Only the settings module calls this; the daemon reads and never writes. It lives here
/// rather than there so that both halves get the path from one place -- the Windows head
/// learned what happens when the program that saves a setting and the program that reads it
/// disagree about what they are naming.
[[nodiscard]] bool save_settings(const Settings& settings);

} // namespace dp::wl
