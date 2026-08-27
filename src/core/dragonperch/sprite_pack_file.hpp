// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"
#include "dragonperch/sprites.hpp"

#include <string>
#include <string_view>

namespace dp {

/// A sprite pack read from a file, plus the name of the atlas image it wants.
///
/// The image is not loaded here. Decoding a PNG is an OS service -- WIC on Windows, a
/// library on Linux -- and this file has to stay free of platform anything, so the caller
/// decodes the atlas and hands the id back in.
struct SpritePackFile {
    SpritePack pack;

    /// Relative to the definition file's own directory.
    std::string atlas_file;
};

/// Parses a sprite pack definition.
///
/// The format is INI, to match the config format the rest of the project will use and
/// because KDE's own tooling speaks it:
///
///     [pack]
///     name = Konqi
///     artwork-licence = CC-BY-SA-4.0
///     attribution = KDE community, https://konqi.kde.org/
///     atlas = konqi.png
///     frame-width = 64
///     frame-height = 64
///
///     [walk]
///     frames = 0, 1, 2, 3
///     duration = 130          ; milliseconds, per frame
///     loop = true
///     anchor = 32, 64         ; optional; defaults to bottom-centre of the cell
///
/// Frame numbers index a grid of cells laid out left to right, then top to bottom. Every
/// section other than `[pack]` is an animation, named by its section.
///
/// Throws std::runtime_error with the line number on anything malformed. A sprite pack is
/// authored once and shipped; a silently mis-parsed one would show up as a dragon standing
/// in the wrong place, which is far harder to trace back than a refusal to start.
[[nodiscard]] SpritePackFile parse_sprite_pack(std::string_view text, int atlas_id,
                                               PixelSize atlas_size);

/// Parses only the `atlas` key, so a caller can decode the image before it knows the atlas
/// id it will be given.
[[nodiscard]] std::string parse_atlas_filename(std::string_view text);

} // namespace dp
