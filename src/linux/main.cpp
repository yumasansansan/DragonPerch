// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/host.hpp"
#include "dragonperch/placeholder_pack.hpp"
#include "dragonperch/simulation.hpp"
#include "frame_clock.hpp"
#include "gles_renderer.hpp"
#include "kwin_geometry.hpp"
#include "layer_surface.hpp"
#include "log.hpp"
#include "sprite_pack_loader.hpp"
#include "wayland_display.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace dp::wl {
namespace {

std::atomic<bool> g_stop{false};

void on_signal(int)
{
    // Nothing here but a flag. This runs on whatever thread the signal lands on, and
    // almost nothing else is legal to call.
    g_stop.store(true, std::memory_order_relaxed);
}

int run_pets(int pet_count, std::span<const std::filesystem::path> pack_paths)
{
    WaylandDisplay display;
    display.connect();

    GlesRenderer renderer;
    renderer.create(display);

    // Not started here: PetHost::run does that, and starting it twice would try to claim
    // the D-Bus name twice.
    KWinGeometryProvider world;
    world.set_outputs(display.outputs());

    // Declared before the simulation and never appended to after spawning: a Pet holds a
    // pointer to its pack, so the vector must neither reallocate nor go out of scope first.
    std::vector<SpritePack> packs;
    packs.reserve(pack_paths.size() + 1);
    for (const std::filesystem::path& path : pack_paths) {
        if (std::optional<SpritePack> loaded = load_sprite_pack(path, renderer)) {
            packs.push_back(std::move(*loaded));
        }
    }

    if (packs.empty()) {
        log_line("sprite pack: none found, using the procedural placeholder");
        const std::vector<std::byte> atlas = placeholder_pack::render_atlas();
        packs.push_back(placeholder_pack::create(
            renderer.register_atlas(atlas, placeholder_pack::atlas_size())));
    }

    Simulation simulation;
    std::mt19937 spawn{1};
    std::size_t next = 0;
    for (const OutputInfo& output : display.outputs()) {
        std::uniform_int_distribution<int> across(output.bounds.left() + 64,
                                                  output.bounds.right() - 64);
        for (int i = 0; i < pet_count; ++i) {
            // Round robin rather than at random, so that asking for three pets gets one of
            // each mascot instead of three of whichever the dice picked.
            simulation.spawn(packs[next++ % packs.size()],
                             PixelPoint{across(spawn), output.bounds.top() + 8});
        }
    }

    log_line(std::format("{} pet(s) on {} output(s); Ctrl+C to stop",
                         simulation.pets().size(), display.outputs().size()));

    FrameClock clock{display, renderer.overlays().front().surface()};
    PetHost host{simulation, world, renderer, clock};

    host.run([&] {
        if (clock.disconnected() || g_stop.load(std::memory_order_relaxed)) {
            return true;
        }
        return std::ranges::any_of(renderer.overlays(),
                                   [](const LayerSurface& overlay) { return overlay.closed(); });
    });

    if (!world.heard_from_kwin()) {
        log_line("");
        log_line("The KWin script never said anything, so the pets had nothing to stand on but");
        log_line("the floor. Install and enable it:");
        log_line("    kwin/install.sh");
    }

    world.stop();
    return 0;
}

/// Milestone 7's check, and the one to run first on a new machine.
///
/// Prints what KWin says about the desktop and nothing else -- no EGL, no surfaces, no
/// pets. If the numbers here follow a window as it is dragged, the hard half works, and
/// anything still wrong on screen is the renderer's fault rather than the script's.
int dump_world(int seconds)
{
    WaylandDisplay display;
    display.connect();

    KWinGeometryProvider world;
    world.set_outputs(display.outputs());

    world.set_changed_handler([](const WorldSnapshot& snapshot) {
        log_line("");
        log_line(std::format("--- snapshot {} ---", snapshot.version()));
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

    world.start();
    log_line(std::format("watching for {}s -- drag, resize, open or close a window", seconds));

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

    world.stop();
    return 0;
}

int run(std::span<const std::string_view> args)
{
    const auto has = [&](std::string_view flag) {
        return std::ranges::find(args, flag) != args.end();
    };

    if (has("--help") || has("-h")) {
        log_line("DragonPerch " DRAGONPERCH_VERSION);
        log_line("  --pets N        how many of each mascot (the default with no arguments)");
        log_line("  --pack FILE     use a sprite pack; repeat for more than one");
        log_line("  --dump-world [--hold]  print the walkable edges as KWin reports them");
        return 0;
    }

    if (has("--dump-world")) {
        return dump_world(has("--hold") ? 60 : 15);
    }

    int count = 3;
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--pets") {
            count = std::clamp(std::atoi(std::string{args[i + 1]}.c_str()), 1, 64);
        }
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
    std::signal(SIGINT, &dp::wl::on_signal);
    std::signal(SIGTERM, &dp::wl::on_signal);

    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    try {
        return dp::wl::run(args);
    } catch (const std::exception& error) {
        dp::wl::log_line(std::format("dragonperch: {}", error.what()));
        return 1;
    }
}
