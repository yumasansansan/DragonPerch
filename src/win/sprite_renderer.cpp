// SPDX-License-Identifier: GPL-3.0-or-later
#include "sprite_renderer.hpp"

#include "fullscreen.hpp"
#include "log.hpp"

#include <format>

#include <utility>

namespace dp::win {

SpriteRenderer::SpriteRenderer()
    : device_(GpuDevice::create())
{
}

void SpriteRenderer::set_outputs(std::span<const OutputInfo> outputs)
{
    overlays_.clear();
    overlays_.reserve(outputs.size());

    for (const OutputInfo& output : outputs) {
        PixelRect bounds = output.bounds;
        bounds.height -= fullscreen_inset;
        overlays_.push_back(Overlay{OutputSurface::create(device_, bounds), output.bounds});
    }

    previous_damage_ = {};
}

int SpriteRenderer::register_atlas(std::span<const std::byte> premultiplied_bgra, PixelSize size)
{
    atlases_.push_back(device_.create_bitmap(premultiplied_bgra, size));
    return static_cast<int>(atlases_.size()) - 1;
}

void SpriteRenderer::begin_frame()
{
    pending_.clear();
}

void SpriteRenderer::draw(const SpriteDraw& sprite)
{
    pending_.push_back(sprite);
}

void SpriteRenderer::end_frame()
{
    PixelRect damage = previous_damage_;
    for (const SpriteDraw& sprite : pending_) {
        damage = damage.united(PixelRect{sprite.destination.x, sprite.destination.y,
                                         sprite.source.width, sprite.source.height});
    }

    // A pixel of slack so nothing is left behind by rounding at the edges.
    damage = damage.inflated(1, 1);

    for (Overlay& overlay : overlays_) {
        // Checked every frame rather than on a timer: a game going full screen is exactly
        // the moment the pets have to be gone.
        const bool should_show = !fullscreen::covers(overlay.monitor);

        if (should_show != overlay.surface.visible()) {
            log_line(std::format("output ({},{}): {} a full-screen app",
                                 overlay.monitor.left(), overlay.monitor.top(),
                                 should_show ? "showing after" : "hiding for"));
        }
        overlay.surface.set_visible(should_show);

        if (!should_show) {
            continue;
        }

        const PixelRect on_this_output = damage.intersect(overlay.surface.bounds());
        if (on_this_output.empty()) {
            continue;
        }

        overlay.surface.draw(on_this_output, [this](ID2D1DeviceContext* d2d) {
            for (const SpriteDraw& sprite : pending_) {
                if (sprite.atlas_id < 0
                    || static_cast<std::size_t>(sprite.atlas_id) >= atlases_.size()) {
                    continue;
                }

                ID2D1Bitmap1* atlas = atlases_[static_cast<std::size_t>(sprite.atlas_id)].Get();

                const auto x = static_cast<float>(sprite.destination.x);
                const auto y = static_cast<float>(sprite.destination.y);
                const auto w = static_cast<float>(sprite.source.width);
                const auto h = static_cast<float>(sprite.source.height);

                const D2D1_RECT_F source = D2D1::RectF(
                    static_cast<float>(sprite.source.left()),
                    static_cast<float>(sprite.source.top()),
                    static_cast<float>(sprite.source.right()),
                    static_cast<float>(sprite.source.bottom()));

                const D2D1_MATRIX_3X2_F previous = [&] {
                    D2D1_MATRIX_3X2_F m{};
                    d2d->GetTransform(&m);
                    return m;
                }();

                if (sprite.flip_x) {
                    // Mirror about the sprite's own centre. A plain negative scale would
                    // reflect through the origin and swing it across the screen.
                    const float centre = x + (w / 2.0F);
                    d2d->SetTransform(D2D1::Matrix3x2F::Translation(-centre, 0.0F)
                                      * D2D1::Matrix3x2F::Scale(-1.0F, 1.0F)
                                      * D2D1::Matrix3x2F::Translation(centre, 0.0F) * previous);
                }

                // Nearest neighbour, not linear. These are pixel-art cells packed side by
                // side in one atlas, and linear sampling bleeds each frame into the next.
                d2d->DrawBitmap(atlas, D2D1::RectF(x, y, x + w, y + h), sprite.opacity,
                                D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &source);

                if (sprite.flip_x) {
                    d2d->SetTransform(previous);
                }
            }
        });
    }

    // One commit for every surface, rather than one per surface: the frame lands on all
    // monitors together.
    if (!overlays_.empty()) {
        check(device_.dcomp()->Commit(), "IDCompositionDesktopDevice::Commit");
    }

    previous_damage_ = {};
    for (const SpriteDraw& sprite : pending_) {
        previous_damage_ = previous_damage_.united(PixelRect{
            sprite.destination.x, sprite.destination.y, sprite.source.width, sprite.source.height});
    }
}

} // namespace dp::win
