// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_geometry.hpp"

#include "dragonperch/edge_builder.hpp"
#include "log.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <systemd/sd-bus.h>

namespace dp::wl {
namespace {

constexpr const char* bus_name = "org.dragonperch.Geometry";
constexpr const char* object_path = "/org/dragonperch/Geometry";
constexpr const char* interface_name = "org.dragonperch.Geometry1";

/// Anything narrower is a tooltip or the visible sliver of something mostly hidden, not a
/// perch. Matches the Windows scanner, and for the same reason.
constexpr int minimum_window_width = 64;

/// KWin identifies a window by a UUID string. The core wants a stable integer, and only
/// that it is stable -- it is compared, never interpreted. FNV-1a over the text gives one
/// for free, with no table to keep and nothing to invalidate when a window closes.
std::int64_t hash_id(std::string_view text) noexcept
{
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001B3ULL;
    }
    return static_cast<std::int64_t>(hash);
}

/// Splits on single spaces. The report is machine-written, so this does not have to cope
/// with runs of whitespace, quoting or anything else -- and if it ever does, that is a
/// change in the script and both halves move together.
std::vector<std::string_view> split(std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0;

    while (start <= line.size()) {
        const std::size_t space = line.find(' ', start);
        if (space == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, space - start));
        start = space + 1;
    }
    return fields;
}

bool to_int(std::string_view text, int& out) noexcept
{
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && ptr == text.data() + text.size();
}

} // namespace

KWinGeometryProvider::KWinGeometryProvider() = default;

KWinGeometryProvider::~KWinGeometryProvider()
{
    stop();
}

void KWinGeometryProvider::set_outputs(std::span<const OutputInfo> outputs)
{
    const std::lock_guard lock{mutex_};
    outputs_.assign(outputs.begin(), outputs.end());
}

const WorldSnapshot& KWinGeometryProvider::current() const
{
    const std::lock_guard lock{mutex_};
    return snapshot_;
}

void KWinGeometryProvider::set_changed_handler(ChangedHandler handler)
{
    handler_ = std::move(handler);
}

void KWinGeometryProvider::start()
{
    if (started_) {
        return;
    }
    started_ = true;

    if (const int failed = sd_bus_open_user(&bus_); failed < 0) {
        throw std::runtime_error(
            std::format("cannot reach the session bus: {}", std::strerror(-failed)));
    }

    // Declared here rather than at namespace scope so it can name a private member. The
    // table is read by sd-bus for as long as the slot lives, so it has to outlive this
    // call -- hence static.
    static const sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Update", "s", "", &KWinGeometryProvider::on_update,
                      SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END,
    };

    if (const int failed = sd_bus_add_object_vtable(bus_, &slot_, object_path, interface_name,
                                                    vtable, this);
        failed < 0) {
        throw std::runtime_error(
            std::format("cannot publish {}: {}", object_path, std::strerror(-failed)));
    }

    // No replace, no queue: a second DragonPerch must fail here rather than silently take
    // the name from the first and leave it drawing a desktop that never changes again.
    if (const int failed = sd_bus_request_name(bus_, bus_name, 0); failed < 0) {
        throw std::runtime_error(std::format(
            "cannot claim {} -- another DragonPerch is probably already running: {}", bus_name,
            std::strerror(-failed)));
    }

    // Publish what is known before any report arrives: the outputs, and a floor on each. A
    // pet spawned before KWin has said anything then lands on the bottom of the screen
    // rather than falling for ever.
    apply("v 1");

    worker_ = std::thread{[this] { run(); }};
    log_line(std::format("listening on {} for the KWin script", bus_name));
}

void KWinGeometryProvider::run()
{
    while (!stopping_.load(std::memory_order_relaxed)) {
        const int processed = sd_bus_process(bus_, nullptr);
        if (processed < 0) {
            log_line(std::format("session bus error: {}", std::strerror(-processed)));
            return;
        }
        if (processed > 0) {
            // More may be queued behind it; go round again without waiting.
            continue;
        }

        // A timeout rather than an indefinite wait, so that stopping does not depend on
        // KWin sending one last message to wake this thread up.
        if (const int failed = sd_bus_wait(bus_, 200'000); failed < 0) {
            log_line(std::format("session bus wait failed: {}", std::strerror(-failed)));
            return;
        }
    }
}

void KWinGeometryProvider::stop() noexcept
{
    stopping_.store(true, std::memory_order_relaxed);
    if (worker_.joinable()) {
        worker_.join();
    }

    if (slot_ != nullptr) {
        sd_bus_slot_unref(slot_);
        slot_ = nullptr;
    }
    if (bus_ != nullptr) {
        sd_bus_unref(bus_);
        bus_ = nullptr;
    }
}

int KWinGeometryProvider::on_update(sd_bus_message* message, void* userdata, sd_bus_error*)
{
    const char* report = nullptr;
    if (const int failed = sd_bus_message_read(message, "s", &report); failed < 0) {
        return failed;
    }

    static_cast<KWinGeometryProvider*>(userdata)->apply(report == nullptr ? "" : report);
    return sd_bus_reply_method_return(message, "");
}

void KWinGeometryProvider::apply(std::string_view report)
{
    std::vector<WindowCandidate> candidates;
    std::vector<WalkableEdge> edges;
    std::vector<OutputInfo> outputs;

    {
        const std::lock_guard lock{mutex_};
        outputs = outputs_;
    }

    std::size_t start = 0;
    while (start <= report.size()) {
        const std::size_t newline = report.find('\n', start);
        const std::string_view line =
            report.substr(start, newline == std::string_view::npos ? std::string_view::npos
                                                                   : newline - start);
        start = newline == std::string_view::npos ? report.size() + 1 : newline + 1;

        if (line.empty()) {
            continue;
        }

        const std::vector<std::string_view> fields = split(line);

        if (fields[0] == "s" && fields.size() == 6) {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            if (!to_int(fields[2], x) || !to_int(fields[3], y) || !to_int(fields[4], width)
                || !to_int(fields[5], height)) {
                continue;
            }

            // Matched by connector name -- "DP-1", "eDP-1" -- which is what wl_output.name
            // reports on one side and what KWin calls the screen on the other. There is no
            // other identifier the two halves share.
            const auto it = std::ranges::find(outputs, fields[1], &OutputInfo::name);
            if (it != outputs.end()) {
                it->work_area = PixelRect{x, y, width, height};
            }
            continue;
        }

        if (fields[0] == "w" && fields.size() == 8) {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            int z = 0;
            int kind = 0;
            if (!to_int(fields[2], x) || !to_int(fields[3], y) || !to_int(fields[4], width)
                || !to_int(fields[5], height) || !to_int(fields[6], z) || !to_int(fields[7], kind)) {
                continue;
            }

            candidates.push_back(WindowCandidate{
                .id = hash_id(fields[1]),
                .frame = PixelRect{x, y, width, height},
                .z = z,
                .kind = kind == 1 ? EdgeKind::panel_top : EdgeKind::window_top,
            });
        }
    }

    // The same occlusion pass the Windows scanner uses, and the same code: a title bar
    // covered by the window in front of it is not somewhere a pet can stand, whichever
    // compositor is doing the covering.
    append_window_edges(candidates, minimum_window_width, edges);

    for (const OutputInfo& output : outputs) {
        edges.push_back(WalkableEdge{
            .owner_id = -output.id - 1,
            .y = output.work_area.bottom(),
            .left = output.work_area.left(),
            .right = output.work_area.right(),
            .kind = EdgeKind::screen_floor,
            .z_order = 0,
        });
    }

    // Into the order the core's lookups assume: by y, then front to back. Skipping this
    // does not fail loudly -- edge_below simply returns the wrong ledge, and a pet lands
    // on the window behind the one it was standing over.
    WorldSnapshot::sort(edges);

    WorldSnapshot snapshot;
    {
        const std::lock_guard lock{mutex_};
        outputs_ = outputs;
        snapshot_ = WorldSnapshot{++version_, std::move(edges), outputs};
        snapshot = snapshot_;
    }

    reports_.fetch_add(1, std::memory_order_relaxed);
    if (handler_) {
        handler_(snapshot);
    }
}

} // namespace dp::wl
