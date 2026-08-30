// SPDX-License-Identifier: GPL-3.0-or-later
#include "gles_renderer.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"
#include "wayland_display.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace dp::wl {
namespace {

constexpr const char* vertex_source = R"(#version 300 es
in vec2 a_position;      // in the overlay's logical units, origin top-left
in vec2 a_texel;         // in atlas texels
in float a_opacity;

uniform vec2 u_viewport; // the overlay's logical size
uniform vec2 u_atlas;    // the atlas size in texels

out vec2 v_uv;
out float v_opacity;

void main()
{
    vec2 unit = a_position / u_viewport;

    // Y down, which is what every rectangle in this program means, and up is what GL
    // means. Flipping here rather than in the projection keeps the sprite maths readable.
    gl_Position = vec4(unit.x * 2.0 - 1.0, 1.0 - unit.y * 2.0, 0.0, 1.0);

    v_uv = a_texel / u_atlas;
    v_opacity = a_opacity;
}
)";

constexpr const char* fragment_source = R"(#version 300 es
precision mediump float;

in vec2 v_uv;
in float v_opacity;

uniform sampler2D u_texture;

out vec4 colour;

void main()
{
    // The core hands atlases over as premultiplied *BGRA*, which is what Direct2D wants on
    // the other head. GL ES has no BGRA internal format worth relying on -- the extension
    // that adds one is not universal, and llvmpipe is a target here -- so the bytes are
    // uploaded as RGBA and swizzled back in the one place it costs nothing.
    colour = texture(u_texture, v_uv).bgra;

    // Premultiplied, so a whole-sprite fade scales colour and alpha together.
    colour *= v_opacity;
}
)";

GLuint compile(GLenum stage, const char* source)
{
    const GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }

    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string message(static_cast<std::size_t>(std::max(length, 1)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, message.data());
    glDeleteShader(shader);

    throw std::runtime_error(cat("shader failed to compile: ", message));
}

} // namespace

GlesRenderer::~GlesRenderer()
{
    // Deleting a GL object needs a current context, and the surface that makes one current
    // belongs to an overlay -- so the order here is: make current, drop the GL objects,
    // then let the overlays and the context go.
    if (overlays_.empty()) {
        return;
    }

    try {
        egl_.make_current(overlays_.front().egl_surface());
    } catch (const std::exception&) {
        // The compositor has already gone. Everything below is about to be freed with the
        // process anyway, and throwing out of a destructor would turn a tidy exit into an
        // abort.
        overlays_.clear();
        return;
    }

    for (const Atlas& atlas : atlases_) {
        glDeleteTextures(1, &atlas.texture);
    }
    if (vertex_array_ != 0) {
        glDeleteVertexArrays(1, &vertex_array_);
    }
    if (vertex_buffer_ != 0) {
        glDeleteBuffers(1, &vertex_buffer_);
    }
    if (program_ != 0) {
        glDeleteProgram(program_);
    }
    overlays_.clear();
}

void GlesRenderer::create(WaylandDisplay& display)
{
    egl_.create(display.display());

    overlays_.resize(display.outputs().size());
    for (std::size_t i = 0; i < display.outputs().size(); ++i) {
        overlays_[i].create(display, egl_, display.outputs()[i]);
    }

    if (overlays_.empty()) {
        throw std::runtime_error("no outputs to draw on");
    }

    egl_.make_current(overlays_.front().egl_surface());
    log_line(cat("gl: ", reinterpret_cast<const char*>(glGetString(GL_RENDERER)), " / ",
                 reinterpret_cast<const char*>(glGetString(GL_VERSION))));

    build_program();
}

void GlesRenderer::build_program()
{
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source);

    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);

    // Bound before linking, so the attribute locations are known rather than queried.
    glBindAttribLocation(program_, 0, "a_position");
    glBindAttribLocation(program_, 1, "a_texel");
    glBindAttribLocation(program_, 2, "a_opacity");

    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint length = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
        std::string message(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetProgramInfoLog(program_, length, nullptr, message.data());
        throw std::runtime_error(cat("shader program failed to link: ", message));
    }

    viewport_uniform_ = glGetUniformLocation(program_, "u_viewport");
    atlas_size_uniform_ = glGetUniformLocation(program_, "u_atlas");
    texture_uniform_ = glGetUniformLocation(program_, "u_texture");

    glGenVertexArrays(1, &vertex_array_);
    glGenBuffers(1, &vertex_buffer_);

    glBindVertexArray(vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);

    const auto stride = static_cast<GLsizei>(sizeof(Vertex));

    // The last argument of glVertexAttribPointer is declared const void* and is not a
    // pointer: with a buffer bound it is a byte offset into that buffer, and the cast is
    // how every GL program passes one. performance-no-int-to-ptr is right in general and
    // has nothing to offer here -- the API has no overload that takes an integer.
    //
    // Hoisted into their own lines so the suppressions land on them. NOLINTNEXTLINE covers
    // exactly the line after it, and clang-tidy reports this at the sub-expression, which
    // in a wrapped call is a continuation line rather than the line the statement starts
    // on -- so a suppression above the call does nothing at all.
    //
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const auto* const texture_offset = reinterpret_cast<const void*>(offsetof(Vertex, u));
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const auto* const opacity_offset = reinterpret_cast<const void*>(offsetof(Vertex, opacity));

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, texture_offset);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, opacity_offset);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

int GlesRenderer::register_atlas(std::span<const std::byte> premultiplied_bgra, PixelSize size)
{
    if (size.width <= 0 || size.height <= 0
        || premultiplied_bgra.size()
               < static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4) {
        throw std::runtime_error("atlas is smaller than the size it claims");
    }
    if (overlays_.empty()) {
        throw std::runtime_error("register_atlas before create: there is no GL context yet");
    }

    egl_.make_current(overlays_.front().egl_surface());

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // GL_NEAREST both ways. These sprites are drawn at exactly the size they were
    // generated at, so linear filtering would only blur them -- and at 52 pixels tall
    // there is nothing to spare.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.width, size.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 premultiplied_bgra.data());

    atlases_.push_back(Atlas{texture, size});
    return static_cast<int>(atlases_.size()) - 1;
}

void GlesRenderer::begin_frame()
{
    pending_.clear();
}

void GlesRenderer::draw(const SpriteDraw& sprite)
{
    pending_.push_back(sprite);
}

void GlesRenderer::end_frame()
{
    // Once, not once per overlay: grouping by atlas is what turns one draw call per pet
    // into one per mascot, and the order is the same for every surface.
    std::ranges::stable_sort(pending_, {}, &SpriteDraw::atlas_id);

    for (LayerSurface& overlay : overlays_) {
        if (!overlay.closed()) {
            flush(overlay);
        }
    }
}

void GlesRenderer::flush(LayerSurface& overlay)
{
    egl_.make_current(overlay.egl_surface());

    const PixelSize buffer = overlay.buffer_size();
    glViewport(0, 0, buffer.width, buffer.height);

    // Transparent, not black. The clear colour is what shows through everywhere a dragon
    // is not, which is almost the entire screen.
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);

    // Premultiplied source. GL_SRC_ALPHA here instead would darken every edge pixel by its
    // own alpha a second time, which shows up as a dark fringe around each sprite.
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(program_);
    glUniform2f(viewport_uniform_, static_cast<float>(overlay.bounds().width),
                static_cast<float>(overlay.bounds().height));
    glUniform1i(texture_uniform_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);

    // One draw call per atlas, so three mascots cost three calls rather than one per pet.
    // end_frame has already grouped them.
    std::size_t start = 0;
    while (start < pending_.size()) {
        const int atlas_id = pending_[start].atlas_id;
        std::size_t end = start;
        while (end < pending_.size() && pending_[end].atlas_id == atlas_id) {
            ++end;
        }

        if (atlas_id < 0 || static_cast<std::size_t>(atlas_id) >= atlases_.size()) {
            start = end;
            continue;
        }
        const Atlas& atlas = atlases_[static_cast<std::size_t>(atlas_id)];

        vertices_.clear();
        for (std::size_t i = start; i < end; ++i) {
            const SpriteDraw& sprite = pending_[i];

            // Into the overlay's own coordinates. A sprite belonging to another monitor
            // ends up outside this surface and is dropped rather than clipped, which is
            // one rectangle test instead of a scissor.
            const float left = static_cast<float>(sprite.destination.x - overlay.bounds().x);
            const float top = static_cast<float>(sprite.destination.y - overlay.bounds().y);
            const float right = left + static_cast<float>(sprite.source.width);
            const float bottom = top + static_cast<float>(sprite.source.height);

            if (right <= 0.0F || bottom <= 0.0F
                || left >= static_cast<float>(overlay.bounds().width)
                || top >= static_cast<float>(overlay.bounds().height)) {
                continue;
            }

            float u0 = static_cast<float>(sprite.source.x);
            float u1 = static_cast<float>(sprite.source.right());
            if (sprite.flip_x) {
                std::swap(u0, u1);
            }
            const auto v0 = static_cast<float>(sprite.source.y);
            const auto v1 = static_cast<float>(sprite.source.bottom());

            const Vertex tl{left, top, u0, v0, sprite.opacity};
            const Vertex tr{right, top, u1, v0, sprite.opacity};
            const Vertex bl{left, bottom, u0, v1, sprite.opacity};
            const Vertex br{right, bottom, u1, v1, sprite.opacity};

            // Two triangles, written out rather than indexed: at this many sprites an
            // index buffer is more code and no less work.
            vertices_.insert(vertices_.end(), {tl, bl, tr, tr, bl, br});
        }

        if (!vertices_.empty()) {
            glBindTexture(GL_TEXTURE_2D, atlas.texture);
            glUniform2f(atlas_size_uniform_, static_cast<float>(atlas.size.width),
                        static_cast<float>(atlas.size.height));
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)),
                         vertices_.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
        }

        start = end;
    }

    glBindVertexArray(0);
    egl_.swap(overlay.egl_surface());
}

} // namespace dp::wl
