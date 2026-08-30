// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_geometry.hpp"

#include "dragonperch/edge_builder.hpp"
#include "dragonperch/text.hpp"
#include "log.hpp"
#include "session_bus.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <systemd/sd-bus.h>

namespace dp::wl {
namespace {

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

/// The most fields any line has is eight -- `w id x y width height z kind`. Room is kept
/// for one more so that a longer line can be seen to be longer, rather than being
/// truncated to exactly the length that would make it look valid.
constexpr std::size_t maximum_fields = 8;
constexpr std::size_t field_capacity = maximum_fields + 1;

/// One line, split on single spaces.
///
/// Into a fixed array rather than a vector, because a drag produces a report per
/// compositor frame and every report is a line per window -- allocating and growing a
/// vector for each of them is a lot of churn to describe eight words with a known maximum.
///
/// The report is machine-written, so this does not have to cope with runs of whitespace,
/// quoting or anything else; if it ever does, that is a change in the script and both
/// halves move together.
struct Fields {
    std::array<std::string_view, field_capacity> at{};
    std::size_t count = 0;
};

Fields split(std::string_view line)
{
    Fields fields;
    std::size_t start = 0;

    while (start <= line.size() && fields.count < field_capacity) {
        const std::size_t space = line.find(' ', start);
        if (space == std::string_view::npos) {
            fields.at[fields.count++] = line.substr(start);
            break;
        }
        fields.at[fields.count++] = line.substr(start, space - start);
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
KWinGeometryProvider::~KWinGeometryProvider() = default;

void KWinGeometryProvider::set_outputs(std::span<const OutputInfo> outputs)
{
    const std::lock_guard lock{mutex_};
    outputs_.assign(outputs.begin(), outputs.end());
}

WorldSnapshot KWinGeometryProvider::current() const
{
    const std::lock_guard lock{mutex_};
    return snapshot_;
}

void KWinGeometryProvider::set_changed_handler(ChangedHandler handler)
{
    // Not under mutex_: the slot has its own, and clearing has to wait for a call already
    // running rather than for the snapshot to be free. PetHost sets this after start(), so
    // the bus thread can already be delivering when it lands.
    handler_.set(std::move(handler));
}

void KWinGeometryProvider::publish(SessionBus& bus)
{
    // Declared here so it can name a private member, and static so it outlives the call --
    // sd-bus reads the table for as long as the object is published.
    static const sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Update", "s", "", &KWinGeometryProvider::on_update,
                      SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END,
    };

    bus.add_object(object_path, interface_name, vtable, this);
}

void KWinGeometryProvider::start()
{
    if (started_) {
        return;
    }
    started_ = true;

    apply("v 1");
}

int KWinGeometryProvider::on_update(sd_bus_message* message, void* userdata, sd_bus_error*)
{
    const char* report = nullptr;
    if (const int failed = sd_bus_message_read(message, "s", &report); failed < 0) {
        return failed;
    }

    auto* self = static_cast<KWinGeometryProvider*>(userdata);

    // Counted here rather than in apply(), which start() also calls to publish the
    // bootstrap floor. Counting there made heard_from_kwin() true before KWin had said a
    // word, and the "the script never said anything" message -- the one that tells somebody
    // their script is not installed -- could never appear.
    self->reports_.fetch_add(1, std::memory_order_relaxed);
    self->apply(report == nullptr ? "" : report);

    return sd_bus_reply_method_return(message, "");
}

void KWinGeometryProvider::apply(std::string_view report)
{
    if (log_raw_ && !report.empty()) {
        log_line("");
        log_line("--- as KWin sent it ---");
        log_line(report);
    }

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

        const Fields fields = split(line);

        // The script says which format it speaks, and it says it first. Read rather than
        // skipped over, because a mismatch here is the difference between "the pets have
        // nothing to stand on" and "an old copy of the script is still running", and those
        // two look identical from the outside.
        //
        // Said once per change rather than once per report: reports arrive continuously,
        // and a line repeated sixty times a second is not a warning, it is a log file.
        if (fields.at[0] == "v" && fields.count == 2) {
            int version = 0;
            if (to_int(fields.at[1], version)
                && script_version_.exchange(version) != version) {
                if (version == format_version) {
                    log_line(cat("kwin: script format ", version));
                } else {
                    log_line("");
                    log_line(cat("kwin: the script talking speaks format ", version,
                                 ", this build speaks ", format_version, "."));
                    log_line("Almost certainly an old copy in ~/.local/share/kwin/scripts,");
                    log_line("which KWin prefers over the packaged one. Reinstall it:");
                    log_line("    kwin/install.sh");
                    log_line("");
                }
            }
            continue;
        }

        if (fields.at[0] == "s" && fields.count == 6) {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            if (!to_int(fields.at[2], x) || !to_int(fields.at[3], y)
                || !to_int(fields.at[4], width) || !to_int(fields.at[5], height)) {
                continue;
            }

            // Matched by connector name -- "DP-1", "eDP-1" -- which is what wl_output.name
            // reports on one side and what KWin calls the screen on the other. There is no
            // other identifier the two halves share.
            const auto it = std::ranges::find(outputs, fields.at[1], &OutputInfo::name);
            if (it != outputs.end()) {
                it->work_area = PixelRect{x, y, width, height};
            }
            continue;
        }

        if (fields.at[0] == "w" && fields.count == 8) {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            int z = 0;
            int kind = 0;
            if (!to_int(fields.at[2], x) || !to_int(fields.at[3], y)
                || !to_int(fields.at[4], width) || !to_int(fields.at[5], height)
                || !to_int(fields.at[6], z) || !to_int(fields.at[7], kind)) {
                continue;
            }

            candidates.push_back(WindowCandidate{
                .id = hash_id(fields.at[1]),
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

    handler_.call(snapshot);
}

} // namespace dp::wl
