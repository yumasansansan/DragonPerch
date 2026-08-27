// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"
#include "win_headers.hpp"

namespace dp::win {

/// A bare top-level window whose only job is to host a composition visual tree.
///
/// The style combination is the whole point, and it is not the obvious one. It was arrived
/// at by measurement — sending a real click with `SendInput` and checking whether
/// `WM_LBUTTONDOWN` reached a window underneath:
///
/// | extended styles                                       | click passes |
/// |-------------------------------------------------------|--------------|
/// | `WS_EX_LAYERED \| WS_EX_TRANSPARENT`                   | yes          |
/// | `WS_EX_NOREDIRECTIONBITMAP \| WS_EX_TRANSPARENT`       | **no**       |
/// | all three together                                     | yes          |
///
/// So:
///
/// - `WS_EX_NOREDIRECTIONBITMAP` — the window owns no pixels; content comes from the
///   composition tree. This is what allows real per-pixel alpha from the GPU.
/// - `WS_EX_LAYERED` — **not** an alternative to the above, despite appearances. It is what
///   makes `WS_EX_TRANSPARENT` actually pass clicks. On its own `WS_EX_TRANSPARENT` does
///   not, whatever the window procedure answers to `WM_NCHITTEST`; answering
///   `HTTRANSPARENT` is not a substitute and testing that answer is not a click-through
///   test. The layered window is given attributes once and its own bitmap is never used.
/// - `WS_EX_TOOLWINDOW` — out of Alt-Tab and the taskbar.
/// - `WS_EX_NOACTIVATE` — never takes focus, so typing is never interrupted.
///
/// Never add `WS_DISABLED`: a disabled window discards clicks rather than letting them
/// fall through.
///
/// The window must also be strictly smaller than its monitor. `SHQueryUserNotificationState`
/// returns `QUNS_BUSY` for a topmost borderless window covering a monitor exactly, and
/// Windows turns on Do Not Disturb. One pixel of inset clears it. That heuristic is about
/// size alone — not transparency, not topmost-ness, not composition.
class OverlayWindow {
public:
    static OverlayWindow create(const PixelRect& bounds);

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;
    OverlayWindow(OverlayWindow&& other) noexcept;
    OverlayWindow& operator=(OverlayWindow&& other) noexcept;
    ~OverlayWindow();

    [[nodiscard]] HWND handle() const noexcept { return hwnd_; }
    [[nodiscard]] const PixelRect& bounds() const noexcept { return bounds_; }

    void move_to(const PixelRect& bounds);

    /// Drains this thread's queue. Returns false once `WM_QUIT` has arrived.
    static bool pump();

private:
    OverlayWindow(HWND hwnd, const PixelRect& bounds) noexcept;

    static void ensure_class_registered();
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND hwnd_ = nullptr;
    PixelRect bounds_{};
};

} // namespace dp::win
