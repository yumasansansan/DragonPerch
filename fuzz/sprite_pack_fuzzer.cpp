// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragonperch/geometry.hpp"
#include "dragonperch/sprite_pack_file.hpp"
#include "dragonperch/sprites.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <span>
#include <string>
#include <string_view>

/// A sprite pack definition, which is the one file here that might not have come from us.
///
/// Packs are meant to be authored and shipped, but the format is documented and the loader
/// searches directories a person can put things in, so a pack from somewhere else is a
/// thing that will happen. It is allowed to be refused -- parse_sprite_pack throws with a
/// line number -- and not allowed to do anything worse.
///
/// The frame numbers are the interesting part. They index a grid of cells, and the
/// rectangles come out of arithmetic on numbers the file chose, against an atlas size it
/// did not: a frame index of two billion against a 384x64 sheet is exactly the shape of
/// question a fuzzer is good at.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    // The size of the atlas the packs that ship actually use, so the arithmetic the parser
    // does is the arithmetic it does in practice.
    constexpr dp::PixelSize atlas{384, 64};

    try {
        (void)dp::parse_atlas_filename(text);
    // Swallowing it is the point: a refusal is the documented behaviour, and what this
    // target watches for is everything else.
    // NOLINTNEXTLINE(bugprone-empty-catch)
    } catch (const std::exception&) {
    }

    try {
        const dp::SpritePackFile file = dp::parse_sprite_pack(text, 0, atlas);

        // Every frame of every animation asked for its source rectangle, because a pack
        // that parses and then produces a cell reaching off the sheet is worse than one
        // that was refused: it reaches the renderer.
        for (std::string_view name : {"walk", "idle", "fall", "land", "turn", "fly"}) {
            if (!file.pack.has(name)) {
                continue;
            }

            const dp::Animation& animation = file.pack.require(name);
            const dp::PixelRect sheet{0, 0, atlas.width, atlas.height};

            // Both directions: a pack that draws its own left-facing frames supplies a
            // second list, and it is indexed by the same arithmetic on the same numbers.
            for (std::span<const dp::AnimationFrame> frames :
                 {animation.frames(), animation.frames_left()}) {
                for (const dp::AnimationFrame& frame : frames) {
                    const dp::PixelRect& source = frame.source;
                    const bool inside =
                        source.width >= 0 && source.height >= 0 && source.left() >= sheet.left()
                        && source.top() >= sheet.top() && source.right() <= sheet.right()
                        && source.bottom() <= sheet.bottom();
                    if (!inside) {
                        std::abort();
                    }
                }
            }
        }
    // Swallowing it is the point: a refusal is the documented behaviour, and what this
    // target watches for is everything else.
    // NOLINTNEXTLINE(bugprone-empty-catch)
    } catch (const std::exception&) {
        // A refusal, which is the documented behaviour for anything malformed.
    }

    return 0;
}
