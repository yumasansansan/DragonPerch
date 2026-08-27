// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/pack_library.hpp"

#include <filesystem>

namespace dp::wl {

/// Decodes a PNG into the layout the renderer registers atlases in: premultiplied BGRA,
/// top-down. Throws on failure.
///
/// BGRA rather than RGBA even though GL would rather have RGBA, because the core's
/// interface names one layout and the Windows head cannot change -- Direct2D has no RGBA
/// premultiplied format. The swizzle costs one shader instruction; see gles_renderer.cpp.
[[nodiscard]] dp::DecodedImage decode_image(const std::filesystem::path& file);

} // namespace dp::wl
