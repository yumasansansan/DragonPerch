// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/pack_library.hpp"
#include "dragonperch/render.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace dp::wl {

/// The core's pack loader, wired to libpng and to where this executable lives.
///
/// The finding and the parsing are in `dragonperch/pack_library.hpp`, shared with the
/// Windows head. All that is platform-specific about it is which library decodes the PNG.
[[nodiscard]] std::optional<SpritePack> load_sprite_pack(const std::filesystem::path& definition,
                                                         ISpriteRenderer& renderer);

/// Every pack shipped with the build, when none is named on the command line.
[[nodiscard]] std::vector<std::filesystem::path> default_sprite_pack_paths();

} // namespace dp::wl
