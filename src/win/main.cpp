// SPDX-License-Identifier: GPL-3.0-or-later
#include "console.hpp"
#include "dcomp_renderer.hpp"
#include "desktop_scanner.hpp"
#include "dragonperch/geometry.hpp"
#include "log.hpp"
#include "overlay_window.hpp"
#include "win_event_watcher.hpp"
#include "win_headers.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <format>
#include <span>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
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

/// Class and caption behind an edge, so a bogus ledge can be identified by eye rather than
/// guessed at. The first time the C# prototype printed this, three suspicious full-width
/// ledges at y=0 turned out to be three perfectly ordinary maximised windows -- which is
/// how the need for occlusion clipping was found.
std::string describe(const WalkableEdge& edge)
{
    if (edge.kind != EdgeKind::window_top) {
        // Cast for the format: these ids are sentinels and negated HMONITORs, and "0x-1"
        // reads worse than the unsigned form.
        return std::format("owner=0x{:X}", static_cast<std::uint64_t>(edge.owner_id));
    }

    auto hwnd = reinterpret_cast<HWND>(static_cast<std::intptr_t>(edge.owner_id));

    std::array<wchar_t, 128> cls{};
    const int cls_length = GetClassNameW(hwnd, cls.data(), static_cast<int>(cls.size()));

    std::array<wchar_t, 128> text{};
    const int text_length = GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size()));

    return std::format("[{}] \"{}\"",
                       to_utf8(std::wstring_view{cls.data(), static_cast<std::size_t>(cls_length)}),
                       to_utf8(std::wstring_view{text.data(), static_cast<std::size_t>(text_length)}));
}

void print(const WorldSnapshot& snapshot)
{
    log_line("");
    log_line(std::format("--- snapshot {} ---", snapshot.version()));

    for (const OutputInfo& output : snapshot.outputs()) {
        log_line(std::format("  output {:<14} bounds=({},{})-({},{}) work=({},{})-({},{}) scale={:.2f}",
                             output.name, output.bounds.left(), output.bounds.top(),
                             output.bounds.right(), output.bounds.bottom(), output.work_area.left(),
                             output.work_area.top(), output.work_area.right(),
                             output.work_area.bottom(), output.scale));
    }

    log_line(std::format("  {} walkable edges:", snapshot.edges().size()));
    for (const WalkableEdge& edge : snapshot.edges()) {
        log_line(std::format("    {:<13} y={:>6}  x={:>6}..{:<6} w={:>5}  {}",
                             kind_name(edge.kind), edge.y, edge.left, edge.right, edge.width(),
                             describe(edge)));
    }
}

/// Milestone 3.
///
/// The renderer and the simulation are not connected yet, so this prints what the scanner
/// found instead: run it, drag a window, and check the numbers move the way the real title
/// bar does.
int dump_world(int seconds)
{
    WinEventWatcher watcher;
    watcher.set_changed_handler(&print);
    watcher.start();

    log_line("");
    log_line(std::format("watching for {}s -- drag, resize, open or close a window", seconds));

    // Nothing to pump on this thread: the watcher owns its own message loop, because
    // SetWinEventHook delivers through the queue of whichever thread installed it.
    std::this_thread::sleep_for(std::chrono::seconds(seconds));

    log_line("");
    log_line(std::format("hook callbacks: {} accepted, {} filtered out",
                         watcher.events_seen(), watcher.events_filtered()));
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

    if (has(L"--dump-world")) {
        log_line(std::format("log: {}", log_path()));
        return dump_world(has(L"--hold") ? 60 : 15);
    }

    log_line("DragonPerch " DRAGONPERCH_VERSION);
    log_line("  --probe-composition [--hold]   milestone 1: draw through DirectComposition");
    log_line("  --dump-world [--hold]          milestone 3: print the walkable edges as they change");
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
