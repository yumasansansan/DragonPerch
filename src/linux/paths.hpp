// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>

namespace dp::wl {

/// The directory holding this executable.
///
/// Everything found at runtime is found relative to here rather than through a path baked
/// in at configure time: the artwork, the translations, the tray icon and the KWin script.
/// That is what lets an unpacked tarball work without being installed.
///
/// One copy, because there were three: the pack loader, the tray and the KWin script each
/// had their own, and the settings module wanted a fourth.
[[nodiscard]] std::filesystem::path executable_directory();

} // namespace dp::wl
