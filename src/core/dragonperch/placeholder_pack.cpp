// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/placeholder_pack.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dp::placeholder_pack {
namespace {

// Roughly Konqi's palette: green body, paler belly, darker wing, dark eye. Alpha is 0xFF
// throughout, so these values are already premultiplied.
constexpr std::uint32_t colour_body = 0xFF3DAA35;
constexpr std::uint32_t colour_belly = 0xFF9BD86F;
constexpr std::uint32_t colour_wing = 0xFF2E7D28;
constexpr std::uint32_t colour_eye = 0xFF101010;

void plot(std::vector<std::byte>& pixels, PixelSize atlas, int x, int y, std::uint32_t argb)
{
    if (x < 0 || y < 0 || x >= atlas.width || y >= atlas.height) {
        return;
    }

    const std::size_t i = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(atlas.width))
                           + static_cast<std::size_t>(x))
                          * 4U;

    pixels[i + 0] = static_cast<std::byte>(argb & 0xFFU);          // B
    pixels[i + 1] = static_cast<std::byte>((argb >> 8U) & 0xFFU);  // G
    pixels[i + 2] = static_cast<std::byte>((argb >> 16U) & 0xFFU); // R
    pixels[i + 3] = static_cast<std::byte>((argb >> 24U) & 0xFFU); // A
}

void draw_leg(std::vector<std::byte>& pixels, PixelSize atlas, int x, int y, int length)
{
    for (int dy = 0; dy < length; ++dy) {
        for (int dx = 0; dx < 3; ++dx) {
            plot(pixels, atlas, x + dx, y + dy, colour_body);
        }
    }
}

void draw_dragon(std::vector<std::byte>& pixels, PixelSize atlas, int origin_x, int bob,
                 bool legs_tucked, int leg_phase)
{
    const int cx = frame_size / 2;
    const int base_y = frame_size - 4 + bob;

    // Wing, behind the body.
    for (int y = -18; y <= -8; ++y) {
        const int half = 18 + y;
        for (int x = -half; x <= 0; ++x) {
            plot(pixels, atlas, origin_x + cx + x - 4, base_y + y, colour_wing);
        }
    }

    // Body: a squat ellipse.
    for (int y = -20; y <= -2; ++y) {
        const double ny = (y + 11) / 9.0;
        const double half_width = 8.0 * std::sqrt(std::max(0.0, 1.0 - (ny * ny)));
        const int half = static_cast<int>(half_width);
        for (int x = -half; x <= half; ++x) {
            const bool belly = x > -3 && y > -10;
            plot(pixels, atlas, origin_x + cx + x, base_y + y, belly ? colour_belly : colour_body);
        }
    }

    // Snout.
    for (int y = -19; y <= -14; ++y) {
        for (int x = 6; x <= 12; ++x) {
            plot(pixels, atlas, origin_x + cx + x, base_y + y, colour_body);
        }
    }

    // Eye on the right, because every frame is authored facing right; the renderer mirrors
    // for the other direction.
    for (int y = -19; y <= -17; ++y) {
        for (int x = 4; x <= 6; ++x) {
            plot(pixels, atlas, origin_x + cx + x, base_y + y, colour_eye);
        }
    }

    if (legs_tucked) {
        return;
    }

    // Two legs, alternating, so the walk cycle reads as a walk.
    const int front = leg_phase == 0 ? 3 : 1;
    const int back = leg_phase == 0 ? 1 : 3;
    draw_leg(pixels, atlas, origin_x + cx + 3, base_y - 3, front);
    draw_leg(pixels, atlas, origin_x + cx - 5, base_y - 3, back);
}

AnimationFrame frame(int index, double seconds)
{
    return AnimationFrame{
        .source = PixelRect{index * frame_size, 0, frame_size, frame_size},
        // The anchor is the feet: bottom-centre of the cell.
        .anchor = PixelOffset{frame_size / 2, frame_size},
        .duration = Duration{seconds},
    };
}

} // namespace

std::vector<std::byte> render_atlas()
{
    const PixelSize size = atlas_size();
    std::vector<std::byte> pixels(static_cast<std::size_t>(size.width)
                                      * static_cast<std::size_t>(size.height) * 4U,
                                  std::byte{0});

    for (int index = 0; index < frame_count; ++index) {
        // Walk frames bob by a pixel; the fall frame tucks its legs up.
        const int bob = (index == 1 || index == 3) ? 1 : 0;
        const bool tucked = index == 5;
        draw_dragon(pixels, size, index * frame_size, bob, tucked, index % 2);
    }

    return pixels;
}

SpritePack create(int atlas_id)
{
    std::map<std::string, Animation, std::less<>> animations;

    animations.emplace("walk", Animation{"walk",
                                         {frame(0, 0.13), frame(1, 0.13), frame(2, 0.13),
                                          frame(3, 0.13)},
                                         true});
    animations.emplace("idle", Animation{"idle", {frame(4, 0.40)}, true});
    animations.emplace("turn", Animation{"turn", {frame(4, 0.35)}, false});
    animations.emplace("fall", Animation{"fall", {frame(5, 0.20)}, true});
    animations.emplace("land", Animation{"land", {frame(4, 0.20)}, false});
    animations.emplace("fly", Animation{"fly", {frame(5, 0.20)}, true});

    return SpritePack{"placeholder",
                      "Placeholder pet",
                      "GPL-3.0-or-later",
                      "Generated by DragonPerch; replace with Konqi artwork.",
                      atlas_id,
                      std::move(animations)};
}

} // namespace dp::placeholder_pack
