// SPDX-License-Identifier: GPL-3.0-or-later
#include "sprite_pack_loader.hpp"

#include "dragonperch/sprite_pack_file.hpp"
#include "log.hpp"
#include "png.hpp"
#include "win_headers.hpp"

#include <array>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace dp::win {
namespace {

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error(std::format("cannot open {}", path.string()));
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return std::move(buffer).str();
}

std::filesystem::path executable_directory()
{
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    return std::filesystem::path{std::wstring_view{buffer.data(), length}}.parent_path();
}

} // namespace

std::filesystem::path default_sprite_pack_path()
{
    const std::filesystem::path relative{"assets/konqi/konqi.ini"};

    // Beside the executable first -- that is where an install puts it -- then up the tree,
    // so running straight out of build/windows-x64/src/win/Debug also finds the assets in
    // the source checkout without a copy step.
    std::filesystem::path directory = executable_directory();
    for (int i = 0; i < 8; ++i) {
        std::filesystem::path candidate = directory / relative;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }

        if (!directory.has_parent_path() || directory.parent_path() == directory) {
            break;
        }
        directory = directory.parent_path();
    }

    return executable_directory() / relative;
}

std::optional<SpritePack> load_sprite_pack(const std::filesystem::path& definition,
                                           SpriteRenderer& renderer)
{
    if (!std::filesystem::exists(definition)) {
        return std::nullopt;
    }

    const std::string text = read_file(definition);

    // Two passes over the definition: the atlas has to be decoded before the frame grid can
    // be worked out, because the cell count comes from the image's real size rather than
    // from anything stated in the file. Stating it twice would be one more thing to get out
    // of step.
    const std::filesystem::path atlas_path =
        definition.parent_path() / parse_atlas_filename(text);

    const DecodedImage atlas = decode_image(atlas_path);
    const int atlas_id = renderer.register_atlas(atlas.pixels, atlas.size);

    SpritePackFile loaded = parse_sprite_pack(text, atlas_id, atlas.size);

    log_line(std::format("sprite pack: {} ({}x{} atlas from {})", loaded.pack.display_name(),
                         atlas.size.width, atlas.size.height, atlas_path.filename().string()));
    if (!loaded.pack.attribution().empty()) {
        log_line(std::format("  artwork: {} -- {}", loaded.pack.artwork_licence(),
                             loaded.pack.attribution()));
    }

    return std::move(loaded.pack);
}

} // namespace dp::win
