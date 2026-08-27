// SPDX-License-Identifier: GPL-3.0-or-later
#include "console.hpp"
#include "desktop_scanner.hpp"
#include "dragonperch/host.hpp"
#include "dragonperch/placeholder_pack.hpp"
#include "dragonperch/simulation.hpp"
#include "frame_clock.hpp"
#include "dragonperch/geometry.hpp"
#include "log.hpp"
#include "overlay_window.hpp"
#include "self_test.hpp"
#include "sprite_renderer.hpp"
#include "win_event_watcher.hpp"
#include "win_headers.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <format>
#include <span>
#include <atomic>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dp::win {
namespace {

/// Set from the console control handler, which runs on its own thread.
std::atomic<bool> g_stop{false};

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

/// Milestone 1, kept as a regression check on the GPU path.
///
/// Draws an opaque quad, a half-transparent one overlapping it, and an outline near the
/// edges. Those answer as much as one screenshot can: whether anything appears at all,
/// whether the background is genuinely transparent rather than black, whether alpha blends
/// against the desktop behind, and whether the surface is placed without an offset.
int probe_composition(int seconds)
{
    report_notification_state("before");

    const PixelRect screen{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};

    SpriteRenderer renderer;
    const OutputInfo output{0, screen, screen, 1.0, "probe"};
    renderer.set_outputs(std::span{&output, 1});

    log_line(std::format("adapter: {}", to_utf8(renderer.device().adapter_description())));
    report_notification_state("with overlay");

    OutputSurface surface = OutputSurface::create(renderer.device(),
                                                  PixelRect{screen.x, screen.y, screen.width,
                                                            screen.height - 1});

    surface.draw(surface.bounds(), [&](ID2D1DeviceContext* d2d) {
        const auto w = static_cast<float>(surface.bounds().width);
        const auto h = static_cast<float>(surface.bounds().height);

        ComPtr<ID2D1SolidColorBrush> brush;

        check(d2d->CreateSolidColorBrush(D2D1::ColorF(0.24F, 0.67F, 0.21F, 1.00F), &brush),
              "CreateSolidColorBrush");
        d2d->FillRectangle(D2D1::RectF(w * 0.10F, h * 0.20F, w * 0.35F, h * 0.55F), brush.Get());

        brush.Reset();
        check(d2d->CreateSolidColorBrush(D2D1::ColorF(0.90F, 0.30F, 0.10F, 0.50F), &brush),
              "CreateSolidColorBrush");
        d2d->FillRectangle(D2D1::RectF(w * 0.25F, h * 0.35F, w * 0.55F, h * 0.70F), brush.Get());

        brush.Reset();
        check(d2d->CreateSolidColorBrush(D2D1::ColorF(1.0F, 1.0F, 1.0F, 0.9F), &brush),
              "CreateSolidColorBrush");
        d2d->DrawRectangle(D2D1::RectF(2.0F, 2.0F, w - 2.0F, h - 2.0F), brush.Get(), 4.0F);
    });

    check(renderer.device().dcomp()->Commit(), "Commit");
    log_line(std::format("drawn; holding for {}s", seconds));

    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < until) {
        if (!OverlayWindow::pump()) {
            break;
        }
        DwmFlush();
    }

    renderer.device().drain_debug_messages();
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

/// Milestone 4: the simulation, the window tracking and the GPU path, connected.
int run_pets(int pet_count)
{
    WinEventWatcher world;
    world.start();

    SpriteRenderer renderer;
    renderer.set_outputs(world.current().outputs());
    log_line(std::format("adapter: {}", to_utf8(renderer.device().adapter_description())));

    // TODO: replace with a real Konqi sheet from assets/konqi/.
    const std::vector<std::byte> atlas = placeholder_pack::render_atlas();
    const int atlas_id = renderer.register_atlas(atlas, placeholder_pack::atlas_size());
    const SpritePack pack = placeholder_pack::create(atlas_id);

    Simulation simulation;
    std::mt19937 spawn{1};
    for (const OutputInfo& output : world.current().outputs()) {
        std::uniform_int_distribution<int> across(output.bounds.left() + 64,
                                                  output.bounds.right() - 64);
        for (int i = 0; i < pet_count; ++i) {
            simulation.spawn(pack, PixelPoint{across(spawn), output.bounds.top() + 8});
        }
    }

    log_line(std::format("{} pet(s) on {} output(s); Ctrl+C or close the console to stop",
                         simulation.pets().size(), world.current().outputs().size()));

    DwmFrameClock clock;
    PetHost host{simulation, world, renderer, clock};

    host.run([] {
        // The overlay windows live on this thread, so their queue has to be drained or they
        // stop answering WM_NCHITTEST and click-through quietly stops working.
        return !OverlayWindow::pump() || g_stop.load(std::memory_order_relaxed);
    });

    renderer.device().drain_debug_messages();
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

    if (has(L"--self-test")) {
        log_line(std::format("log: {}", log_path()));
        return self_test::run();
    }

    if (has(L"--pets") || args.empty()) {
        int count = 3;
        for (std::size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == L"--pets") {
                count = std::clamp(_wtoi(std::wstring{args[i + 1]}.c_str()), 1, 64);
            }
        }
        log_line(std::format("log: {}", log_path()));
        return run_pets(count);
    }

    if (has(L"--dump-world")) {
        log_line(std::format("log: {}", log_path()));
        return dump_world(has(L"--hold") ? 60 : 15);
    }

    log_line("DragonPerch " DRAGONPERCH_VERSION);
    log_line("  --probe-composition [--hold]   milestone 1: draw through DirectComposition");
    log_line("  --dump-world [--hold]          milestone 3: print the walkable edges as they change");
    log_line("  --pets N                       run the pets (the default with no arguments)");
    log_line("  --self-test                    click-through and notification-state check");
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
