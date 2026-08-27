// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"
#include "dragonperch/render.hpp"
#include "dragonperch/sprites.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace dp {

/// A decoded image, ready to hand to a renderer: premultiplied BGRA, top-down,
/// stride = `size.width * 4`.
struct DecodedImage {
    std::vector<std::byte> pixels;
    PixelSize size{};
};

/// How a head turns a PNG on disk into pixels. WIC on Windows, libpng on Linux -- neither
/// of which the core is allowed to know about, which is why this is a parameter.
///
/// Must throw rather than return an empty image: a pack that names an atlas it cannot
/// decode is an authoring mistake, and carrying on would show it as an invisible dragon.
using ImageDecoder = std::function<DecodedImage(const std::filesystem::path&)>;

/// Loads a sprite pack from a definition file and registers its atlas with the renderer.
///
/// Returns nothing if the file is missing, so a build with no artwork yet still runs on the
/// procedural placeholder. A file that exists but does not parse throws instead: that is an
/// authoring mistake too, and falling back silently would show it as a dragon standing in
/// the wrong place rather than as an error.
[[nodiscard]] std::optional<SpritePack> load_sprite_pack(const std::filesystem::path& definition,
                                                         ISpriteRenderer& renderer,
                                                         const ImageDecoder& decode);

/// Every pack shipped with the build: one per mascot, `assets/<id>/<id>.ini`.
///
/// `start` is where to begin looking -- the directory holding the executable. The `assets`
/// directory is looked for there and then up the tree, so running straight out of a build
/// directory finds the artwork in the source checkout without a copy step.
///
/// Returns empty rather than throwing when there is no `assets` directory at all.
[[nodiscard]] std::vector<std::filesystem::path>
find_sprite_packs(const std::filesystem::path& start);

} // namespace dp
