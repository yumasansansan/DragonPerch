// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"
#include "dragonperch/pack_library.hpp"

#include <cstddef>
#include <filesystem>
#include <span>

namespace dp::win {

/// Decodes an image file into the layout the renderer registers atlases in.
/// Throws std::system_error or std::runtime_error on failure.
[[nodiscard]] DecodedImage decode_image(const std::filesystem::path& file);

#ifdef DRAGONPERCH_DIAGNOSTICS
/// Writes premultiplied BGRA out as a PNG. Used by --export-placeholder, which produces a
/// working pack an artist can open and replace cell by cell -- an authoring tool, so it
/// goes wherever the rest of the diagnostics go.
void encode_png(const std::filesystem::path& file, std::span<const std::byte> premultiplied_bgra,
                PixelSize size);
#endif

} // namespace dp::win
