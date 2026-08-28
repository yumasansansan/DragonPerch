// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"
#include "dragonperch/sprites.hpp"

#include <cstddef>
#include <vector>

namespace dp {

/// A procedurally drawn stand-in sprite pack.
///
/// It began as the answer to having no artwork: a crude winged blob, enough to see whether
/// a pet lands on the right pixel row, faces the right way, and animates. There is real
/// artwork now, so it is no longer a runtime fallback -- a head that finds no packs says so
/// instead, which tells somebody more than green blobs do and keeps 11.5 KB out of a
/// release build.
///
/// What it is still for: `--export-placeholder` writes it out as a working sprite pack for
/// an artist to open and replace cell by cell, and the tests build worlds with it.
///
/// It lives in the core rather than in a backend because both heads need something to
/// draw, and generating premultiplied BGRA is exactly as portable as the rest of this
/// library. Delete it once assets/konqi/ has real sheets.
namespace placeholder_pack {

inline constexpr int frame_size = 40;
inline constexpr int frame_count = 6;

[[nodiscard]] constexpr PixelSize atlas_size() noexcept
{
    return PixelSize{frame_size * frame_count, frame_size};
}

/// Premultiplied BGRA, top-down, stride = `atlas_size().width * 4`.
[[nodiscard]] std::vector<std::byte> render_atlas();

/// Frames are laid out left to right: four walk, one idle, one fall.
[[nodiscard]] SpritePack create(int atlas_id);

} // namespace placeholder_pack
} // namespace dp
