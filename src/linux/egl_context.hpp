// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <EGL/egl.h>

struct wl_display;
struct wl_egl_window;

namespace dp::wl {

/// The EGL display, config and context, shared by every overlay surface.
///
/// One context for all outputs rather than one each: the atlas textures and the shader are
/// created once and used by every surface, and sharing a context is what makes that legal
/// without `EGL_KHR_*` share-group juggling.
class EglContext {
public:
    EglContext() = default;
    ~EglContext();

    EglContext(const EglContext&) = delete;
    EglContext& operator=(const EglContext&) = delete;
    EglContext(EglContext&&) = delete;
    EglContext& operator=(EglContext&&) = delete;

    void create(wl_display* display);

    /// A window surface for one overlay. Throws on failure.
    [[nodiscard]] EGLSurface create_surface(wl_egl_window* window);
    void destroy_surface(EGLSurface surface) noexcept;

    void make_current(EGLSurface surface);
    void swap(EGLSurface surface);

    [[nodiscard]] EGLDisplay display() const noexcept { return display_; }

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
};

} // namespace dp::wl
