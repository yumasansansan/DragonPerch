// SPDX-License-Identifier: GPL-3.0-or-later
#include "console.hpp"
#include "desktop_scanner.hpp"
#include "dragonperch/host.hpp"
#include "dragonperch/simulation.hpp"
#include "frame_clock.hpp"
#include "dragonperch/geometry.hpp"
#include "dragonperch/text.hpp"
#include "log.hpp"
#include "overlay_window.hpp"
#ifdef DRAGONPERCH_DIAGNOSTICS
#include "dragonperch/placeholder_pack.hpp"
#include "self_test.hpp"
#endif
#include "control.hpp"
#include "png.hpp"
#include "settings_file.hpp"
#include "sprite_pack_loader.hpp"
#include "sprite_renderer.hpp"
#include "tray.hpp"
#include "win_event_watcher.hpp"
#include "win_headers.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <format>
#include <span>
#include <stdexcept>
#include <utility>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dp::win {
namespace {

/// Set from the console control handler, which runs on its own thread.
std::atomic<bool> g_stop{false};

void request_stop()
{
    g_stop.store(true, std::memory_order_relaxed);
}

#ifdef DRAGONPERCH_DIAGNOSTICS

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

#endif // DRAGONPERCH_DIAGNOSTICS

/// Milestone 4: the simulation, the window tracking and the GPU path, connected.
int run_pets(int pet_count, std::span<const std::filesystem::path> pack_paths)
{
    WinEventWatcher world;
    world.start();

    // Bound to a local: current() hands back a copy now, and a span into a temporary
    // would dangle before it was read.
    const WorldSnapshot world_now = world.current();

    SpriteRenderer renderer;
    renderer.set_outputs(world_now.outputs());
    log_line(cat("adapter: ", to_utf8(renderer.device().adapter_description())));

    // Declared before the simulation and never appended to after spawning: a Pet holds a
    // pointer to its pack, so the vector must neither reallocate nor go out of scope first.
    std::vector<SpritePack> packs;
    packs.reserve(pack_paths.size() + 1);
    for (const std::filesystem::path& path : pack_paths) {
        if (std::optional<SpritePack> loaded = load_sprite_pack(path, renderer)) {
            packs.push_back(std::move(*loaded));
        }
    }

    // No silent fallback. The procedural placeholder is still generated by
    // --export-placeholder, which is what it is for -- an artist opens it and replaces the
    // cells -- but standing green blobs on somebody's title bars because an install went
    // wrong tells them less than saying so does, and it cost 11.5 KB of a 402 KB binary to
    // keep the option open.
    if (packs.empty()) {
        throw std::runtime_error("no sprite packs found. Expected assets/<id>/<id>.ini beside "
                                 "the executable, in ../share/dragonperch, or up the tree from "
                                 "a build directory");
    }

    Settings settings = load_settings();
    if (pet_count > 0) {
        // An explicit --pets beats the file. Somebody who typed a number expects that
        // number, whatever they saved last week.
        settings.pets_per_mascot = pet_count;
    }

    Simulation simulation;
    std::mt19937 spawn{1};

    // Spawning is something that has to be doable twice, because reloading settings that
    // change the cast cannot be done any other way. The outputs are read afresh each time:
    // a reload may be the first thing that happens after a monitor was plugged in.
    const auto populate = [&] {
        simulation.clear_pets();
        simulation.set_options(settings.to_options());

        const WorldSnapshot now = world.current();
        for (const OutputInfo& output : now.outputs()) {
            if (!settings.wants_output(output.name)) {
                continue;
            }

            std::uniform_int_distribution<int> across(output.bounds.left() + 64,
                                                      output.bounds.right() - 64);
            for (const SpritePack& pack : packs) {
                if (!settings.wants_mascot(pack.id())) {
                    continue;
                }
                // One loop per mascot rather than one round-robin loop over a count, so
                // that "two each" means two each and not two divided between three.
                for (int i = 0; i < settings.pets_per_mascot; ++i) {
                    simulation.spawn(pack, PixelPoint{across(spawn), output.bounds.top() + 8});
                }
            }
        }
    };

    populate();

    log_line(cat(simulation.pets().size(), " pet(s) on ", world_now.outputs().size(),
                 " output(s)"));
    log_line("to stop: the tray icon, or close the console, or  dragonperch --stop");

    DwmFrameClock clock;
    PetHost host{simulation, world, renderer, clock};

    // One handler for both the control interface and the tray, so the two cannot drift
    // into meaning different things by the same name. Everything it touches lives on this
    // thread -- the messages arrive through the same pump the overlays use -- so unlike the
    // Wayland head it can reach the simulation directly rather than through a flag.
    const auto command = [&](Command what) {
        log_line(cat("command: ", name_of(what)));
        switch (what) {
        case Command::quit:
            g_stop.store(true, std::memory_order_relaxed);
            break;
        case Command::pause:
            host.set_paused(true);
            break;
        case Command::resume:
            host.set_paused(false);
            break;
        case Command::toggle_pause:
            host.set_paused(!host.paused());
            break;
        case Command::reload: {
            const Settings fresh = load_settings();

            // Only a change to the cast needs the pets spawning again. Adjusting a walk
            // speed while one is mid-stride should not teleport it back to the top.
            const bool respawn = settings.needs_respawn(fresh);
            settings = fresh;

            if (respawn) {
                populate();
                log_line(cat(simulation.pets().size(), " pet(s) after reload"));
            } else {
                simulation.set_options(settings.to_options());
            }
            break;
        }
        }
    };

    // On this thread, so the handler runs between frames rather than alongside one -- the
    // messages arrive through the same pump the overlay windows use.
    ControlServer control;
    if (!control.start(command)) {
        log_line("control: no window, so --stop and the tray will not reach this instance");
    }

    // Same thread as the overlays and the control window, so its menu runs between frames
    // and the handler can touch anything the loop owns.
    TrayIcon tray;
    if (!tray.add(command, [&host] { return host.paused(); })) {
        log_line("tray: no icon; use the console or  dragonperch --stop");
    }

    host.run([] {
        // The overlay windows live on this thread, so their queue has to be drained or they
        // stop answering WM_NCHITTEST and click-through quietly stops working. The tray's
        // window and the control window are on it too, and are pumped by the same call.
        return !OverlayWindow::pump() || g_stop.load(std::memory_order_relaxed);
    });

    renderer.device().drain_debug_messages();
    return 0;
}

#ifdef DRAGONPERCH_DIAGNOSTICS

/// Writes the procedural placeholder out as a real sprite pack: a PNG atlas and the
/// definition that goes with it.
///
/// This is the template. An artist replaces the cells in the PNG and adjusts the durations;
/// nothing about the format has to be learnt from prose. It also means the file-loading
/// path is exercised on every machine, rather than only on one that happens to have Konqi's
/// artwork sitting in the right directory.
int export_placeholder(const std::filesystem::path& directory)
{
    std::filesystem::create_directories(directory);

    const PixelSize size = placeholder_pack::atlas_size();
    const std::vector<std::byte> pixels = placeholder_pack::render_atlas();

    const std::filesystem::path png = directory / "konqi.png";
    encode_png(png, pixels, size);

    const std::filesystem::path ini = directory / "konqi.ini";
    std::ofstream out(ini, std::ios::binary);
    // A raw string literal, so the file below reads exactly as it will be written.
    out << R"(; Written by dragonperch --export-placeholder. Replace the cells in konqi.png
; with real artwork and adjust the durations. The format is documented in
; assets/konqi/README.md.

[pack]
id = placeholder
name = Placeholder dragon
artwork-licence = GPL-3.0-or-later
attribution = Generated by DragonPerch
atlas = konqi.png
frame-width = )"
        << placeholder_pack::frame_size << R"(
frame-height = )"
        << placeholder_pack::frame_size << R"(

[walk]
frames = 0, 1, 2, 3
duration = 130

[idle]
frames = 4
duration = 400

[turn]
frames = 4
duration = 350
loop = false

[land]
frames = 4
duration = 200
loop = false

[fall]
frames = 5
duration = 200

[fly]
frames = 5
duration = 200
)";


    log_line(std::format("wrote {} and {}", png.string(), ini.string()));
    return 0;
}

#endif // DRAGONPERCH_DIAGNOSTICS

int run(std::span<const std::wstring_view> args)
{
    const auto has = [&](std::wstring_view flag) {
        return std::ranges::find(args, flag) != args.end();
    };

    attach_parent_console();
    handle_console_stop(&request_stop);

    if (has(L"--version")) {
        log_line(DRAGONPERCH_VERSION);
        return 0;
    }

    // One flag per command, all going through the same message.
    for (const auto& [flag, command] : {
             std::pair{L"--stop", Command::quit},
             std::pair{L"--pause", Command::pause},
             std::pair{L"--resume", Command::resume},
             std::pair{L"--reload", Command::reload},
         }) {
        if (!has(flag)) {
            continue;
        }
        if (!send_command(command)) {
            log_line("no DragonPerch is running in this session");
            return 1;
        }
        log_line(cat("asked it to ", name_of(command)));
        return 0;
    }

#ifdef DRAGONPERCH_DIAGNOSTICS
    if (has(L"--probe-composition")) {
        log_line(std::format("log: {}", log_path()));
        return probe_composition(has(L"--hold") ? 30 : 8);
    }
#endif

    const auto value_after = [&](std::wstring_view flag) -> std::optional<std::wstring> {
        for (std::size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == flag) {
                return std::wstring{args[i + 1]};
            }
        }
        return std::nullopt;
    };

#ifdef DRAGONPERCH_DIAGNOSTICS
    if (const std::optional<std::wstring> directory = value_after(L"--export-placeholder")) {
        log_line(std::format("log: {}", log_path()));
        return export_placeholder(*directory);
    }

    if (has(L"--self-test")) {
        log_line(std::format("log: {}", log_path()));
        return self_test::run();
    }
#endif

    if (has(L"--pets") || args.empty()) {
        // Zero means "whatever the settings file says", which is what running with no
        // arguments has to mean now that there is a settings file.
        int count = 0;
        for (std::size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == L"--pets") {
                count = std::clamp(_wtoi(std::wstring{args[i + 1]}.c_str()), 1, 64);
            }
        }
        log_line(cat("log: ", log_path()));

        // --pack may be given more than once; the pets are shared out between whatever is
        // named. With none named, every mascot shipped with the build joins in.
        std::vector<std::filesystem::path> packs;
        for (std::size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == L"--pack") {
                packs.emplace_back(args[i + 1]);
            }
        }
        if (packs.empty()) {
            packs = default_sprite_pack_paths();
        }
        return run_pets(count, packs);
    }

#ifdef DRAGONPERCH_DIAGNOSTICS
    if (has(L"--dump-world")) {
        log_line(std::format("log: {}", log_path()));
        return dump_world(has(L"--hold") ? 60 : 15);
    }
#endif

    log_line("DragonPerch " DRAGONPERCH_VERSION);
    log_line("  --pets N                       how many of each mascot; overrides the settings");
    log_line("  --pack FILE                    use a sprite pack; repeat for more than one");
    log_line("  --stop                         ask a running DragonPerch to quit");
    log_line("  --pause / --resume             freeze the pets where they are, or let them go");
    log_line("  --reload                       re-read the settings file");
    log_line("  --version                      print the version and exit");
#ifdef DRAGONPERCH_DIAGNOSTICS
    log_line("  --probe-composition [--hold]   milestone 1: draw through DirectComposition");
    log_line("  --dump-world [--hold]          milestone 3: print the walkable edges as they change");
    log_line("  --self-test                    click-through and notification-state check");
    log_line("  --export-placeholder DIR       write the placeholder out as a pack template");
#else
    log_line("");
    log_line("The diagnostic modes are left out of a release build. Configure with");
    log_line("-D DRAGONPERCH_DIAGNOSTICS=ON for a release binary that still has them.");
#endif
    return 0;
}

} // namespace
} // namespace dp::win

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
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

    // WIC is a COM library, and the sprite pack loader is the only user. Apartment
    // threaded is what a UI process wants and costs nothing here.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

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

    if (SUCCEEDED(com)) {
        CoUninitialize();
    }

    LocalFree(argv);
    return result;
}
