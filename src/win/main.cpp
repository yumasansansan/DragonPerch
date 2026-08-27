// SPDX-License-Identifier: GPL-3.0-or-later
#include "console.hpp"
#include "dcomp_renderer.hpp"
#include "dragonperch/geometry.hpp"
#include "log.hpp"
#include "overlay_window.hpp"
#include "win_headers.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dp::win {
namespace {

void report_notification_state(const char* when)
{
    QUERY_USER_NOTIFICATION_STATE state{};
    if (FAILED(SHQueryUserNotificationState(&state))) {
        log_line(std::format("notification state {}: query failed", when));
        return;
    }

    log_line(std::format("notification state {}: {} {}", when, static_cast<int>(state),
                         state == QUNS_ACCEPTS_NOTIFICATIONS
                             ? "(QUNS_ACCEPTS_NOTIFICATIONS, no Do Not Disturb)"
                             : "(NOT QUNS_ACCEPTS_NOTIFICATIONS)"));
}

/// Milestone 1.
///
/// The one part of the Windows design that was never verified: click-through with
/// `WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP` was measured, but whether composition content
/// actually *renders* on such a window was not. If it does not, the whole Windows plan
/// changes, so this is deliberately the first thing that runs.
///
/// Draws an opaque quad, a half-transparent one overlapping it, and an outline near the
/// edges. Those three answer as much as one screenshot can: whether anything appears at
/// all, whether the background is genuinely transparent rather than black, whether alpha
/// blends against the desktop behind, and whether the surface is placed without an offset.
int probe_composition(int seconds)
{
    const int screen_w = GetSystemMetrics(SM_CXSCREEN);
    const int screen_h = GetSystemMetrics(SM_CYSCREEN);

    // Deliberately not the full monitor. A topmost borderless window matching a monitor
    // exactly makes Windows report QUNS_BUSY and turn on Do Not Disturb.
    const PixelRect bounds{0, 0, screen_w, screen_h - 1};

    report_notification_state("before");

    OverlayWindow window = OverlayWindow::create(bounds);
    log_line(std::format("window: {}x{} at ({},{})", bounds.width, bounds.height, bounds.left(),
                         bounds.top()));

    DcompRenderer renderer = DcompRenderer::create(window.handle(), bounds.size());
    log_line(std::format("adapter: {}", to_utf8(renderer.adapter_description())));

    report_notification_state("with overlay");

    renderer.draw_frame(PixelRect{0, 0, bounds.width, bounds.height},
                        [&](ID2D1DeviceContext* d2d) {
                            const auto x = static_cast<float>(bounds.width);
                            const auto y = static_cast<float>(bounds.height);

                            ComPtr<ID2D1SolidColorBrush> brush;

                            // Opaque: proves content reaches the screen at all.
                            check(d2d->CreateSolidColorBrush(
                                      D2D1::ColorF(0.24F, 0.67F, 0.21F, 1.00F), &brush),
                                  "CreateSolidColorBrush");
                            d2d->FillRectangle(
                                D2D1::RectF(x * 0.10F, y * 0.20F, x * 0.35F, y * 0.55F),
                                brush.Get());

                            // Half transparent and overlapping: proves alpha blends against
                            // both the desktop behind and other content in the surface.
                            brush.Reset();
                            check(d2d->CreateSolidColorBrush(
                                      D2D1::ColorF(0.90F, 0.30F, 0.10F, 0.50F), &brush),
                                  "CreateSolidColorBrush");
                            d2d->FillRectangle(
                                D2D1::RectF(x * 0.25F, y * 0.35F, x * 0.55F, y * 0.70F),
                                brush.Get());

                            // An outline hugging the edges: proves the surface is not being
                            // scaled or shifted.
                            brush.Reset();
                            check(d2d->CreateSolidColorBrush(
                                      D2D1::ColorF(1.0F, 1.0F, 1.0F, 0.9F), &brush),
                                  "CreateSolidColorBrush");
                            d2d->DrawRectangle(D2D1::RectF(2.0F, 2.0F, x - 2.0F, y - 2.0F),
                                               brush.Get(), 4.0F);
                        });

    log_line(std::format("drawn; holding for {}s", seconds));

    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < until) {
        if (!OverlayWindow::pump()) {
            break;
        }
        // DwmFlush will be the real frame clock. Here it only keeps the loop off a busy
        // spin while the window stays responsive to WM_NCHITTEST.
        DwmFlush();
    }

    renderer.drain_debug_messages();
    log_line("done");
    return 0;
}

int run(std::span<const std::wstring_view> args)
{
    const auto has = [&](std::wstring_view flag) {
        return std::ranges::find(args, flag) != args.end();
    };

    attach_parent_console();

    if (has(L"--probe-composition")) {
        log_line(std::format("log: {}", log_path()));
        return probe_composition(has(L"--hold") ? 30 : 8);
    }

    log_line("DragonPerch " DRAGONPERCH_VERSION);
    log_line("  --probe-composition [--hold]   milestone 1: draw through DirectComposition");
    return 0;
}

} // namespace
} // namespace dp::win

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return 1;
    }

    std::vector<std::wstring_view> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    int result = 1;
    try {
        result = dp::win::run(args);
    } catch (const std::exception& ex) {
        dp::win::attach_parent_console();
        dp::win::log_line(std::string("dragonperch: ") + ex.what());
    } catch (...) {
        dp::win::attach_parent_console();
        dp::win::log_line("dragonperch: unknown exception");
    }

    LocalFree(argv);
    return result;
}
