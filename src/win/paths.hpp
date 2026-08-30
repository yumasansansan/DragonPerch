// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>

namespace dp::win {

/// The directory holding this executable.
///
/// Everything this program finds at runtime is found relative to here rather than through a
/// path baked in at configure time: the artwork, the translations, the tray icon and the
/// shell. That is what lets an unpacked zip work without being installed.
///
/// One copy, because there were two and are about to be three.
[[nodiscard]] std::filesystem::path executable_directory();

} // namespace dp::win
