// SPDX-License-Identifier: GPL-3.0-or-later
#include "egl_context.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"

#include <array>
#include <stdexcept>

#include <EGL/eglext.h>
#include <GLES3/gl3.h>

namespace dp::wl {
namespace {

[[noreturn]] void fail(const char* what)
{
    throw std::runtime_error(cat(what, " failed: EGL error 0x", hex(eglGetError())));
}

} // namespace

EglContext::~EglContext()
{
    if (display_ == EGL_NO_DISPLAY) {
        return;
    }

    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context_ != EGL_NO_CONTEXT) {
        eglDestroyContext(display_, context_);
    }
    eglTerminate(display_);
}

void EglContext::create(wl_display* display)
{
    // eglGetPlatformDisplay rather than eglGetDisplay. The old call guesses the platform
    // from the pointer, and on a machine with both X11 and Wayland EGL available it can
    // guess wrong; naming the platform is unambiguous.
    display_ = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display, nullptr);
    if (display_ == EGL_NO_DISPLAY) {
        fail("eglGetPlatformDisplay");
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (eglInitialize(display_, &major, &minor) != EGL_TRUE) {
        fail("eglInitialize");
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        fail("eglBindAPI");
    }

    // EGL_ALPHA_SIZE is the one that matters and the one that is easy to leave out. Without
    // it EGL is entitled to hand back an opaque config, and then the overlay is a black
    // rectangle over the whole screen rather than a few dragons.
    const std::array<EGLint, 13> attributes{
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE,
    };

    EGLint count = 0;
    if (eglChooseConfig(display_, attributes.data(), &config_, 1, &count) != EGL_TRUE
        || count == 0) {
        throw std::runtime_error("no EGL config with an alpha channel; the overlay would be "
                                 "an opaque rectangle over the whole screen");
    }

    const std::array<EGLint, 3> context_attributes{EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, context_attributes.data());
    if (context_ == EGL_NO_CONTEXT) {
        fail("eglCreateContext");
    }

    log_line(cat("egl: ", major, ".", minor, ", ", eglQueryString(display_, EGL_VENDOR)));
}

EGLSurface EglContext::create_surface(wl_egl_window* window)
{
    EGLSurface surface = eglCreatePlatformWindowSurface(display_, config_, window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        fail("eglCreatePlatformWindowSurface");
    }

    if (eglMakeCurrent(display_, surface, surface, context_) != EGL_TRUE) {
        fail("eglMakeCurrent");
    }

    // Pacing comes from wl_surface.frame, which is the compositor telling us it is ready.
    // Leaving the swap interval at 1 would make eglSwapBuffers block on a frame callback of
    // its own as well, so the loop would wait twice for the same thing.
    eglSwapInterval(display_, 0);
    return surface;
}

void EglContext::destroy_surface(EGLSurface surface) noexcept
{
    if (display_ != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface);
    }
}

void EglContext::make_current(EGLSurface surface)
{
    if (eglMakeCurrent(display_, surface, surface, context_) != EGL_TRUE) {
        fail("eglMakeCurrent");
    }
}

void EglContext::swap(EGLSurface surface)
{
    if (eglSwapBuffers(display_, surface) != EGL_TRUE) {
        fail("eglSwapBuffers");
    }
}

} // namespace dp::wl
