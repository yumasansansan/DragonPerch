// SPDX-License-Identifier: GPL-3.0-or-later
#include "sprite_pack_loader.hpp"

#include "dragonperch/sprite_pack_file.hpp"
#include "log.hpp"
#include "png.hpp"
#include "win_headers.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

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

std::vector<std::filesystem::path> default_sprite_pack_paths()
{
    // Beside the executable first -- that is where an install puts it -- then up the tree,
    // so running straight out of build/windows-x64/src/win/Debug also finds the assets in
    // the source checkout. Konqi is what identifies the directory: an `assets` folder that
    // does not contain him is somebody else's.
    std::filesystem::path directory = executable_directory();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(directory / "assets/konqi/konqi.ini")) {
            break;
        }

        if (!directory.has_parent_path() || directory.parent_path() == directory) {
            return {};
        }
        directory = directory.parent_path();
    }

    // One pack per mascot, named after the directory it lives in. Sorted, so that which
    // dragon spawns where does not depend on the order the filesystem hands them back.
    std::vector<std::filesystem::path> packs;
    std::error_code failed;
    for (const auto& entry : std::filesystem::directory_iterator{directory / "assets", failed}) {
        std::filesystem::path candidate =
            entry.path() / (entry.path().filename().string() + ".ini");
        if (entry.is_directory() && std::filesystem::exists(candidate)) {
            packs.push_back(std::move(candidate));
        }
    }

    std::ranges::sort(packs);
    return packs;
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
