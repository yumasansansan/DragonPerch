// SPDX-License-Identifier: GPL-3.0-or-later
#include "overlay_window.hpp"

#include <utility>

namespace dp::win {
namespace {

constexpr const wchar_t* kClassName = L"DragonPerchOverlay";
ATOM g_class_atom = 0;

} // namespace

OverlayWindow::OverlayWindow(HWND hwnd, const PixelRect& bounds) noexcept
    : hwnd_(hwnd)
    , bounds_(bounds)
{
}

OverlayWindow::OverlayWindow(OverlayWindow&& other) noexcept
    : hwnd_(std::exchange(other.hwnd_, nullptr))
    , bounds_(other.bounds_)
{
}

OverlayWindow& OverlayWindow::operator=(OverlayWindow&& other) noexcept
{
    if (this != &other) {
        if (hwnd_ != nullptr) {
            DestroyWindow(hwnd_);
        }
        hwnd_ = std::exchange(other.hwnd_, nullptr);
        bounds_ = other.bounds_;
    }
    return *this;
}

OverlayWindow::~OverlayWindow()
{
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

void OverlayWindow::ensure_class_registered()
{
    if (g_class_atom != 0) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &OverlayWindow::window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;

    g_class_atom = RegisterClassExW(&wc);
    check_last_error(g_class_atom != 0, "RegisterClassExW");
}

OverlayWindow OverlayWindow::create(const PixelRect& bounds)
{
    ensure_class_registered();

    const HWND hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW
            | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kClassName,
        L"DragonPerch",
        WS_POPUP,
        bounds.left(), bounds.top(), bounds.width, bounds.height,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    check_last_error(hwnd != nullptr, "CreateWindowExW");

    // Fully opaque: the alpha that matters comes from the composition content. This call
    // exists only because a layered window must have been given attributes once, and being
    // layered is what makes WS_EX_TRANSPARENT pass clicks.
    check_last_error(SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA) != FALSE,
                     "SetLayeredWindowAttributes");

    // SW_SHOWNOACTIVATE, not SW_SHOW: appearing must not pull focus from whatever the user
    // is typing into.
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    return OverlayWindow{hwnd, bounds};
}

void OverlayWindow::move_to(const PixelRect& bounds)
{
    if (bounds == bounds_) {
        return;
    }

    check_last_error(
        SetWindowPos(hwnd_, nullptr, bounds.left(), bounds.top(), bounds.width, bounds.height,
                     SWP_NOACTIVATE | SWP_NOZORDER)
            != FALSE,
        "SetWindowPos");

    bounds_ = bounds;
}

bool OverlayWindow::pump()
{
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

LRESULT CALLBACK OverlayWindow::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_NCHITTEST:
        // Belt and braces next to WS_EX_TRANSPARENT. On its own this is not enough --
        // measured -- but it costs one case and it is the correct answer.
        return HTTRANSPARENT;

    case WM_DESTROY:
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace dp::win
