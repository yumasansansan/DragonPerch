// SPDX-License-Identifier: GPL-3.0-or-later
#include "sprite_pack_loader.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"
#include "png.hpp"

#include <string>
#include <system_error>

namespace dp::wl {
namespace {

std::filesystem::path executable_directory()
{
    std::error_code failed;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", failed);
    if (failed) {
        return std::filesystem::current_path();
    }
    return self.parent_path();
}

} // namespace

std::vector<std::filesystem::path> default_sprite_pack_paths()
{
    // Beside the executable first -- that is where an install puts it -- then up the tree,
    // so running straight out of build/linux-x64/src/linux also finds the assets in the
    // source checkout.
    return find_sprite_packs(executable_directory());
}

std::optional<SpritePack> load_sprite_pack(const std::filesystem::path& definition,
                                           ISpriteRenderer& renderer)
{
    std::optional<SpritePack> pack = dp::load_sprite_pack(definition, renderer, &decode_image);
    if (!pack.has_value()) {
        return std::nullopt;
    }

    log_line(cat("sprite pack: ", pack->display_name(), " (from ",
                 definition.filename().string(), ")"));
    if (!pack->attribution().empty()) {
        log_line(cat("  artwork: ", pack->artwork_licence(), " -- ", pack->attribution()));
    }

    return pack;
}

} // namespace dp::wl
