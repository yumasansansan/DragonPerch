// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/host.hpp"
#include "dragonperch/simulation.hpp"
#include "frame_clock.hpp"
#include "gles_renderer.hpp"
#include "control.hpp"
#include "kwin_geometry.hpp"
#include "kwin_script.hpp"
#include "layer_surface.hpp"
#include "dragonperch/text.hpp"
#include "log.hpp"
#include "session_bus.hpp"
#include "settings_file.hpp"
#include "sprite_pack_loader.hpp"
#include "tray.hpp"
#include "wayland_display.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <GLES3/gl3.h>

// sigaction, not std::signal -- see handle_stop_signals.
#include <signal.h>

namespace dp::wl {
namespace {

/// One well-known name for the program, with an object per thing it does. Two names for
/// one process was an accident of the geometry object having arrived first.
///
/// The KWin script calls this name, and the script is installed by the same package as the
/// binary -- so there is no version to be skewed against. A copy left behind in
/// ~/.local/share by an earlier `kwin/install.sh` would shadow the packaged one and go on
/// calling the old name; that shows up as "the KWin script never said anything", which is
/// a message the program already prints along with what to do about it.
constexpr const char* bus_name = "org.dragonperch";

std::atomic<bool> g_stop{false};

/// What the bus thread is allowed to say about pausing.
///
/// A pointer to the PetHost would be simpler to write and wrong: the bus thread would be
/// calling into an object the render loop is about to destroy, and no amount of making the
/// pointer atomic fixes a call that is already running. So the bus thread only ever writes
/// a flag, and the loop reads it -- on Windows the handlers run on the render thread and
/// can touch the host directly, which is the difference between the platforms rather than
/// an inconsistency.
std::atomic<bool> g_paused{false};

/// Set by the bus thread, acted on by the render loop, for the same reason as g_paused:
/// re-reading the settings means spawning and destroying pets, and doing that underneath a
/// frame that is walking the same vector is the one thing the split exists to prevent.
std::atomic<bool> g_reload{false};

void on_signal(int)
{
    // Nothing here but a flag. This runs on whatever thread the signal lands on, and
    // almost nothing else is legal to call.
    g_stop.store(true, std::memory_order_relaxed);
}

void handle_stop_signals()
{
    // sigaction rather than std::signal, and with sa_flags left at zero on purpose.
    // glibc's signal() installs handlers with SA_RESTART, which restarts the poll inside
    // wl_display_dispatch instead of returning EINTR -- so the render loop never wakes and
    // Ctrl+C does nothing at all.
    struct sigaction action = {};
    action.sa_handler = &on_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

/// Asks KWin to re-run the geometry script, so that a picture of the desktop arrives now
/// rather than whenever somebody next moves a window.
void ask_kwin_for_a_report()
{
    const std::filesystem::path script = find_kwin_script();
    if (script.empty()) {
        log_line("kwin: the geometry script is not installed. Run kwin/install.sh -- without"
                 " it the pets have nothing to stand on but the floor");
        return;
    }

    if (reload_kwin_script(script)) {
        log_line(cat("kwin: asked it to re-run ", script.string()));
    }
}

#ifdef DRAGONPERCH_DIAGNOSTICS

/// Milestone 6's check: can this put pixels on the screen at all?
///
/// Clears each overlay to a translucent colour and holds. Nothing else is involved -- no
/// simulation, no atlases, no shader -- so if the screen tints, the layer surface, the EGL
/// config's alpha channel and the compositing all work, and anything still invisible is
/// the sprite path's fault. The Windows head has the same mode, and it earned its keep.
int probe_composition(int seconds)
{
    WaylandDisplay display;
    display.connect();

    GlesRenderer renderer;
    renderer.create(display);

    FrameClock clock{display, renderer.overlays().front().surface()};

    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < until && !g_stop.load(std::memory_order_relaxed)
           && !clock.disconnected()) {
        for (LayerSurface& overlay : renderer.overlays()) {
            renderer.egl().make_current(overlay.egl_surface());
            glViewport(0, 0, overlay.buffer_size().width, overlay.buffer_size().height);

            // Premultiplied, so a quarter-opacity green is (0, 0.25, 0, 0.25).
            glClearColor(0.0F, 0.25F, 0.0F, 0.25F);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.egl().swap(overlay.egl_surface());
        }
        (void)clock.wait_for_next_frame();
    }

    log_line(std::format("held for {}s, {} frame(s) presented", seconds, clock.frames()));
    if (clock.frames() == 0) {
        log_line("No frames at all: the compositor never presented the surface.");
    }
    return 0;
}

#endif // DRAGONPERCH_DIAGNOSTICS

int run_pets(int pet_count, std::span<const std::filesystem::path> pack_paths)
{
    WaylandDisplay display;
    display.connect();

    // Before anything is drawn, and before the overlays exist. Claiming the names is what
    // makes KWin's reports reach us at all, and asking for one has to come after that --
    // the first report is the difference between the pets standing on the panel and
    // standing underneath it.
    SessionBus bus;
    bus.open();
    bus.request_name(bus_name);

    KWinGeometryProvider world;
    world.set_outputs(display.outputs());
    world.publish(bus);

    // One handler for the control interface and the tray, so the two cannot drift into
    // meaning different things by the same name. It runs on the bus thread and touches
    // nothing but the two flags above.
    const auto command = [](Command what) {
        log_line(cat("command: ", name_of(what)));
        switch (what) {
        case Command::quit:
            g_stop.store(true, std::memory_order_relaxed);
            break;
        case Command::pause:
            g_paused.store(true, std::memory_order_relaxed);
            break;
        case Command::resume:
            g_paused.store(false, std::memory_order_relaxed);
            break;
        case Command::toggle_pause:
            g_paused.store(!g_paused.load(std::memory_order_relaxed), std::memory_order_relaxed);
            break;
        case Command::reload:
            g_reload.store(true, std::memory_order_relaxed);
            break;
        }
    };

    ControlService control;
    control.publish(bus, command);

    TrayIcon tray;
    tray.publish(bus, command, [] { return g_paused.load(std::memory_order_relaxed); });

    // The bootstrap floor is published before the bus can deliver anything, not after. The
    // other order has a window in which a real report arrives and is then overwritten by a
    // snapshot that knows only about the floor -- which would drop every pet to the bottom
    // of the screen until KWin next said something.
    world.start();

    // Stops the bus thread before `world` and `control` are destroyed. They are declared
    // after `bus`, so they are destroyed before it, and without this its thread can dispatch
    // a call into an object that is already gone.
    const struct StopOnTheWayOut {
        SessionBus& bus;
        ~StopOnTheWayOut() { bus.stop(); }
    } stopper{bus};

    bus.start();
    ask_kwin_for_a_report();

    // After the bus is running: it is a method call on the watcher, and the reply has to be
    // able to arrive.
    (void)tray.register_with_watcher();

    GlesRenderer renderer;
    renderer.create(display);

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

    // A fixed seed on purpose: two runs on the same desktop put the pets in the same
    // places, which is what makes "it fell through the title bar" reproducible rather than
    // a story. clang-tidy is right that this is not random; being random is not wanted.
    // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
    std::mt19937 spawn{1};

    // Spawning is something that has to be doable twice, because reloading settings that
    // change the cast cannot be done any other way.
    const auto populate = [&] {
        simulation.clear_pets();
        simulation.set_options(settings.to_options());

        for (const OutputInfo& output : display.outputs()) {
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

    log_line(cat(simulation.pets().size(), " pet(s) on ", display.outputs().size(),
                 " output(s); Ctrl+C to stop"));

    FrameClock clock{display, renderer.overlays().front().surface()};
    PetHost host{simulation, world, renderer, clock};

    host.run([&] {
        // The bus thread's flag, applied here rather than there. run() calls this every
        // iteration -- including while paused, every pause_poll -- so resuming is noticed.
        host.set_paused(g_paused.load(std::memory_order_relaxed));

        // Likewise for a reload: read on this thread, between frames, where destroying and
        // spawning pets cannot land in the middle of a step. exchange rather than a load
        // and a store, so two --reloads in the same frame do not become three.
        if (g_reload.exchange(false, std::memory_order_relaxed)) {
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
        }

        if (clock.disconnected() || g_stop.load(std::memory_order_relaxed)) {
            return true;
        }
        return std::ranges::any_of(renderer.overlays(),
                                   [](const LayerSurface& overlay) { return overlay.closed(); });
    });

    log_line(cat(clock.frames(), " frame(s) presented"));
    if (clock.frames() == 0) {
        log_line("None at all, so nothing was ever drawn. Try --probe-composition, which");
        log_line("takes the simulation out of it.");
    }

    if (!world.heard_from_kwin()) {
        log_line("");
        log_line("The KWin script never said anything, so the pets had nothing to stand on but");
        log_line("the floor. Install and enable it:");
        log_line("    kwin/install.sh");
    } else if (world.script_format_version() == 0) {
        // It talked, but never said what it was. Every version that sends a version puts
        // it on the first line, so silence there means a script older than that.
        log_line("");
        log_line("The KWin script never said which format it speaks, so it predates this");
        log_line("build. Reinstall it:");
        log_line("    kwin/install.sh");
    }

    return 0;
}

#ifdef DRAGONPERCH_DIAGNOSTICS

/// Milestone 7's check, and the one to run first on a new machine.
///
/// Prints what KWin says about the desktop and nothing else -- no EGL, no surfaces, no
/// pets. If the numbers here follow a window as it is dragged, the hard half works, and
/// anything still wrong on screen is the renderer's fault rather than the script's.
int dump_world(int seconds)
{
    WaylandDisplay display;
    display.connect();

    SessionBus bus;
    bus.open();
    bus.request_name(bus_name);

    KWinGeometryProvider world;
    world.set_outputs(display.outputs());
    world.log_raw_reports(true);
    world.publish(bus);

    const auto started = std::chrono::steady_clock::now();
    world.set_changed_handler([started](const WorldSnapshot& snapshot) {
        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        log_line("");
        log_line(std::format("--- snapshot {} at +{}ms ---", snapshot.version(), age.count()));
        for (const OutputInfo& output : snapshot.outputs()) {
            log_line(std::format("  output {:<10} {}x{} at {},{}  usable ({},{})-({},{})",
                                 output.name, output.bounds.width, output.bounds.height,
                                 output.bounds.x, output.bounds.y, output.work_area.left(),
                                 output.work_area.top(), output.work_area.right(),
                                 output.work_area.bottom()));
        }
        log_line(std::format("  {} walkable edges:", snapshot.edges().size()));
        for (const WalkableEdge& edge : snapshot.edges()) {
            log_line(std::format("    {:<13} y={:>6}  x={:>6}..{:<6} w={:>5}  owner={:x}",
                                 kind_name(edge.kind), edge.y, edge.left, edge.right, edge.width(),
                                 static_cast<std::uint64_t>(edge.owner_id)));
        }
    });

    // Everything registered before the bus can deliver anything: the handler, and the
    // bootstrap floor. See run_pets for what the other order costs.
    world.start();

    const struct StopOnTheWayOut {
        SessionBus& bus;
        ~StopOnTheWayOut() { bus.stop(); }
    } stopper{bus};

    bus.start();
    ask_kwin_for_a_report();
    log_line(std::format("watching for {}s -- drag, resize, open or close a window", seconds));
    log_line("a snapshot arriving before you touch anything is the KWin script introducing"
             " itself");

    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < until
           && !g_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!world.heard_from_kwin()) {
        log_line("");
        log_line("Nothing arrived. The KWin script is not installed or not enabled:");
        log_line("    kwin/install.sh");
    }

    return 0;
}

#endif // DRAGONPERCH_DIAGNOSTICS

int run(std::span<const std::string_view> args)
{
    const auto has = [&](std::string_view flag) {
        return std::ranges::find(args, flag) != args.end();
    };

    if (has("--version")) {
        log_line(DRAGONPERCH_VERSION);
        return 0;
    }

    // One flag per command, all going through the same method call.
    for (const auto& [flag, command] : {
             std::pair{std::string_view{"--stop"}, Command::quit},
             std::pair{std::string_view{"--pause"}, Command::pause},
             std::pair{std::string_view{"--resume"}, Command::resume},
             std::pair{std::string_view{"--reload"}, Command::reload},
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

    if (has("--help") || has("-h")) {
        log_line("DragonPerch " DRAGONPERCH_VERSION);
        log_line("  --pets N        how many of each mascot; overrides the settings");
        log_line("  --pack FILE     use a sprite pack; repeat for more than one");
        log_line("  --version       print the version and exit");
        log_line("  --stop          ask a running DragonPerch to quit");
        log_line("  --pause / --resume  freeze the pets where they are, or let them go");
        log_line("  --reload        re-read the settings file");
#ifdef DRAGONPERCH_DIAGNOSTICS
        log_line("  --dump-world [--hold]         print the edges as KWin reports them");
        log_line("  --probe-composition [--hold]  tint the screen, and nothing else");
#else
        log_line("");
        log_line("The diagnostic modes are left out of a release build. Configure with");
        log_line("-D DRAGONPERCH_DIAGNOSTICS=ON to get a release binary that still has them.");
#endif
        return 0;
    }

#ifdef DRAGONPERCH_DIAGNOSTICS
    if (has("--probe-composition")) {
        return probe_composition(has("--hold") ? 30 : 8);
    }

    if (has("--dump-world")) {
        return dump_world(has("--hold") ? 60 : 15);
    }
#endif

    // Zero means "whatever the settings file says", which is what running with no
    // arguments has to mean now that there is a settings file.
    int count = 0;
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] != "--pets") {
            continue;
        }

        // from_chars rather than atoi, which cannot report a failure and is undefined on
        // overflow. The clamp below would have turned both into 1, so `--pets abc` ran one
        // pet and said nothing -- which is not what anybody typing that meant.
        const std::string_view text = args[i + 1];
        int wanted = 0;
        const auto [end, failed] =
            std::from_chars(text.data(), text.data() + text.size(), wanted);

        if (failed != std::errc{} || end != text.data() + text.size()) {
            log_line(cat("--pets: '", text, "' is not a number; using the settings instead"));
            continue;
        }

        count = std::clamp(wanted, 1, 64);
    }

    // --pack may be given more than once; the pets are shared out between whatever is
    // named. With none named, every mascot shipped with the build joins in.
    std::vector<std::filesystem::path> packs;
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--pack") {
            packs.emplace_back(args[i + 1]);
        }
    }
    if (packs.empty()) {
        packs = default_sprite_pack_paths();
    }

    return run_pets(count, packs);
}

} // namespace
} // namespace dp::wl

int main(int argc, char** argv)
{
    dp::wl::handle_stop_signals();

    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    try {
        return dp::wl::run(args);
    } catch (const std::exception& error) {
        dp::wl::log_line(dp::cat("dragonperch: ", error.what()));
        return 1;
    }
}
