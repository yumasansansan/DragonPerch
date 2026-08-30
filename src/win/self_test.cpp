// SPDX-License-Identifier: GPL-3.0-or-later
#include "self_test.hpp"

#include "dragonperch/geometry.hpp"
#include "dragonperch/placeholder_pack.hpp"
#include "dragonperch/render.hpp"
#include "dragonperch/world.hpp"
#include "log.hpp"
#include "overlay_window.hpp"
#include "sprite_renderer.hpp"
#include "win_headers.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <span>
#include <thread>
#include <vector>

namespace dp::win::self_test {
namespace {

constexpr const wchar_t* target_class = L"DragonPerchSelfTestTarget";

std::atomic<bool> g_click_received{false};

LRESULT CALLBACK target_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    // 0x0200..0x020E is the legacy mouse range, 0x0240..0x0257 the WM_POINTER range. Log
    // both: a window can be switched to pointer messages without ever seeing a mouse one,
    // and that would otherwise look identical to the click being swallowed.
    if ((msg >= 0x0200 && msg <= 0x020E) || (msg >= 0x0240 && msg <= 0x0257)) {
        log_line(std::format("    target received input message 0x{:04X}", msg));
    }

    if (msg == WM_LBUTTONDOWN) {
        g_click_received.store(true, std::memory_order_relaxed);
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/// Runs a plain window on its own thread with its own pump.
///
/// Its own thread on purpose: if an overlay only broke input for the thread it lives on, a
/// same-thread target would report a failure no real application would ever see.
void run_target(const PixelRect& rect, HANDLE ready)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &target_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = target_class;
    RegisterClassExW(&wc);

    const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, target_class,
                                      L"dragonperch self test target", WS_POPUP | WS_BORDER,
                                      rect.left(), rect.top(), rect.width, rect.height, nullptr,
                                      nullptr, GetModuleHandleW(nullptr), nullptr);

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetEvent(ready);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyWindow(hwnd);
}

void send_click(int x, int y)
{
    // SendInput's absolute coordinates are normalised to 0..65535 across the screen.
    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);

    const auto nx = static_cast<LONG>((static_cast<long long>(x) * 65535) / (width - 1));
    const auto ny = static_cast<LONG>((static_cast<long long>(y) * 65535) / (height - 1));

    const auto mouse = [&](DWORD flags) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = nx;
        input.mi.dy = ny;
        input.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE;
        return input;
    };

    std::array<INPUT, 3> inputs{
        mouse(MOUSEEVENTF_MOVE),
        mouse(MOUSEEVENTF_LEFTDOWN),
        mouse(MOUSEEVENTF_LEFTUP),
    };

    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void report_notification_state(const char* when)
{
    QUERY_USER_NOTIFICATION_STATE state{};
    if (FAILED(SHQueryUserNotificationState(&state))) {
        log_line(std::format("notification state {}: query failed", when));
        return;
    }

    log_line(std::format("notification state {:<14}: {} {}", when, static_cast<int>(state),
                         state == QUNS_ACCEPTS_NOTIFICATIONS
                             ? "(QUNS_ACCEPTS_NOTIFICATIONS, no Do Not Disturb)"
                             : "(NOT QUNS_ACCEPTS_NOTIFICATIONS)"));
}

} // namespace

int run()
{
    log_line("=== dragonperch self test ===");
    log_line("");

    report_notification_state("before");

    const PixelRect target_rect{200, 200, 400, 300};
    const HANDLE ready = check_handle(CreateEventW(nullptr, TRUE, FALSE, nullptr), "CreateEventW");
    std::thread target(run_target, target_rect, ready);
    WaitForSingleObject(ready, 5000);
    CloseHandle(ready);

    const int x = target_rect.left() + (target_rect.width / 2);
    const int y = target_rect.top() + (target_rect.height / 2);

    // The real renderer, not a bare window: whatever the composition stack adds -- a child
    // window, an input sink -- has to be part of what is measured. Testing the window alone
    // would pass while the app still swallowed every click.
    const PixelRect screen{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};

    SpriteRenderer renderer;
    const OutputInfo output{0, screen, screen, 1.0, "self-test"};
    renderer.set_outputs(std::span{&output, 1});

    // Something actually drawn over the click point: a click that lands on a pet is the
    // interesting case, not one that misses.
    const std::vector<std::byte> atlas = placeholder_pack::render_atlas();
    const int atlas_id = renderer.register_atlas(atlas, placeholder_pack::atlas_size());

    renderer.begin_frame();
    renderer.draw(SpriteDraw{
        .atlas_id = atlas_id,
        .source = PixelRect{0, 0, placeholder_pack::frame_size, placeholder_pack::frame_size},
        .destination = PixelPoint{x - (placeholder_pack::frame_size / 2),
                                  y - (placeholder_pack::frame_size / 2)},
    });
    renderer.end_frame();

    OverlayWindow::pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    OverlayWindow::pump();

    report_notification_state("with overlay");
    log_line("");
    log_line(std::format("clicking ({},{}), on the sprite and over the self-test window", x, y));

    POINT restore{};
    GetCursorPos(&restore);

    g_click_received.store(false, std::memory_order_relaxed);
    send_click(x, y);

    for (int i = 0; i < 60 && !g_click_received.load(std::memory_order_relaxed); ++i) {
        OverlayWindow::pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    SetCursorPos(restore.x, restore.y);

    const bool passed = g_click_received.load(std::memory_order_relaxed);
    log_line("");
    log_line(passed ? "PASS: the click reached the window under the overlay."
                    : "FAIL: the overlay swallowed the click.");

    PostThreadMessageW(GetThreadId(target.native_handle()), WM_QUIT, 0, 0);
    target.join();

    return passed ? 0 : 1;
}

} // namespace dp::win::self_test
