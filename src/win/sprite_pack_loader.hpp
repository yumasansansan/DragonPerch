// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/sprites.hpp"
#include "sprite_renderer.hpp"

#include <filesystem>
#include <optional>

namespace dp::win {

/// Loads a sprite pack from a definition file and registers its atlas with the renderer.
///
/// Returns nothing if the file is missing, so a build with no artwork yet still runs on the
/// procedural placeholder. A file that exists but does not parse throws instead: that is an
/// authoring mistake, and falling back silently would show it as a dragon standing in the
/// wrong place rather than as an error.
[[nodiscard]] std::optional<SpritePack> load_sprite_pack(const std::filesystem::path& definition,
                                                         SpriteRenderer& renderer);

/// Where a pack is looked for when none is named: `assets/konqi/konqi.ini`, searched beside
/// the executable and then up the tree, so it works from a build directory as well as from
/// an install.
[[nodiscard]] std::filesystem::path default_sprite_pack_path();

} // namespace dp::win
