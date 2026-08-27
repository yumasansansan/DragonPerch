// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/sprites.hpp"
#include "sprite_renderer.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace dp::win {

/// Loads a sprite pack from a definition file and registers its atlas with the renderer.
///
/// Returns nothing if the file is missing, so a build with no artwork yet still runs on the
/// procedural placeholder. A file that exists but does not parse throws instead: that is an
/// authoring mistake, and falling back silently would show it as a dragon standing in the
/// wrong place rather than as an error.
[[nodiscard]] std::optional<SpritePack> load_sprite_pack(const std::filesystem::path& definition,
                                                         SpriteRenderer& renderer);

/// Every pack shipped with the build, when none is named on the command line.
///
/// One per mascot: `assets/<id>/<id>.ini`. The `assets` directory is searched for beside
/// the executable and then up the tree, so running straight out of a build directory finds
/// the artwork in the source checkout without a copy step.
///
/// Returns empty rather than throwing when there is no `assets` directory at all, so a
/// build with no artwork yet still runs on the procedural placeholder.
[[nodiscard]] std::vector<std::filesystem::path> default_sprite_pack_paths();

} // namespace dp::win
