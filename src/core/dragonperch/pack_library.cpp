// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/pack_library.hpp"

#include "dragonperch/sprite_pack_file.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace dp {
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

} // namespace

std::optional<SpritePack> load_sprite_pack(const std::filesystem::path& definition,
                                           ISpriteRenderer& renderer, const ImageDecoder& decode)
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

    const DecodedImage atlas = decode(atlas_path);
    const int atlas_id = renderer.register_atlas(atlas.pixels, atlas.size);

    SpritePackFile loaded = parse_sprite_pack(text, atlas_id, atlas.size);
    return std::move(loaded.pack);
}

std::vector<std::filesystem::path> find_sprite_packs(const std::filesystem::path& start)
{
    // Konqi is what identifies the directory: an `assets` folder that does not contain him
    // is somebody else's.
    std::filesystem::path directory = start;
    bool found = false;
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(directory / "assets/konqi/konqi.ini")) {
            found = true;
            break;
        }

        if (!directory.has_parent_path() || directory.parent_path() == directory) {
            break;
        }
        directory = directory.parent_path();
    }

    if (!found) {
        return {};
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

} // namespace dp
