// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/render.hpp"
#include "dragonperch/world.hpp"
#include "egl_context.hpp"
#include "layer_surface.hpp"

#include <cstddef>
#include <span>
#include <vector>

#include <GLES3/gl3.h>

namespace dp::wl {

class WaylandDisplay;

/// Draws the simulation's sprites with OpenGL ES 3, one layer-shell overlay per output.
///
/// The same shape as the Windows renderer, for the same reason: the core hands over a flat
/// list of blits once a frame, and the backend decides which surface each one belongs to.
class GlesRenderer final : public ISpriteRenderer {
public:
    GlesRenderer() = default;
    ~GlesRenderer() override;

    /// Connects to EGL and puts an overlay on every output.
    void create(WaylandDisplay& display);

    int register_atlas(std::span<const std::byte> premultiplied_bgra, PixelSize size) override;

    void begin_frame() override;
    void draw(const SpriteDraw& sprite) override;
    void end_frame() override;

    [[nodiscard]] std::span<LayerSurface> overlays() noexcept { return overlays_; }
    [[nodiscard]] EglContext& egl() noexcept { return egl_; }

private:
    struct Atlas {
        GLuint texture = 0;
        PixelSize size{};
    };

    /// Four bytes of position, four of texel, one of alpha -- interleaved, because one
    /// buffer upload a frame is cheaper than three, and at sixty sprites the whole thing
    /// is a few kilobytes.
    struct Vertex {
        float x = 0.0F;
        float y = 0.0F;
        float u = 0.0F;
        float v = 0.0F;
        float opacity = 1.0F;
    };

    void build_program();
    void flush(LayerSurface& overlay);

    EglContext egl_;
    std::vector<LayerSurface> overlays_;
    std::vector<Atlas> atlases_;

    GLuint program_ = 0;
    GLuint vertex_buffer_ = 0;
    GLuint vertex_array_ = 0;
    GLint viewport_uniform_ = -1;   ///< u_viewport, the overlay's logical size
    GLint atlas_size_uniform_ = -1;  ///< u_atlas, the atlas size in texels
    GLint texture_uniform_ = -1;     ///< u_texture, the sampler

    std::vector<SpriteDraw> pending_;
    std::vector<Vertex> vertices_;
};

} // namespace dp::wl
