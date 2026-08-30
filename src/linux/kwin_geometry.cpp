// SPDX-License-Identifier: GPL-3.0-or-later
#include "kwin_geometry.hpp"

#include "dragonperch/edge_builder.hpp"
#include "dragonperch/text.hpp"
#include "log.hpp"
#include "session_bus.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>
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

/// The largest coordinate, in the report's own units, that could describe a real desktop.
///
/// A 16K monitor is 15360 units wide and a wall of a hundred of them still fits in this
/// with three orders of magnitude to spare, so nothing legitimate is turned away.
constexpr int coordinate_limit = 1'000'000;

/// A coordinate or a length, refused unless it could plausibly be one.
///
/// Update is published SD_BUS_VTABLE_UNPRIVILEGED, so the sender is not necessarily the
/// script -- it is whatever in the session decided to call the method. The numbers go
/// straight into PixelRect, whose right() and bottom() are a plain `x + width`, and int is
/// nowhere near wide enough to absorb that when both halves are two billion. Signed
/// overflow is undefined behaviour rather than a wrong answer, and it would happen four
/// functions away inside the shared edge builder rather than here.
///
/// Refused rather than clamped: a line whose numbers cannot describe a screen is not a
/// screen described badly, and inventing a plausible rectangle for it would put a ledge
/// somewhere nobody asked for one.
bool to_coordinate(std::string_view text, int& out) noexcept
{
    int value = 0;
    if (!to_int(text, value) || value < -coordinate_limit || value > coordinate_limit) {
        return false;
    }
    out = value;
    return true;
}

/// A width or a height, refused unless it could plausibly be one.
///
/// Separate from to_coordinate because the two admit different things. A coordinate may be
/// negative -- a monitor placed to the left of the primary one starts at a negative x -- and
/// a length may not.
///
/// Bounding only the magnitude, which is what this did at first, was not enough:
/// `s DP-1 0 0 -920 1032` passed, and a rectangle 920 pixels wide in the wrong direction has
/// a right edge to the left of its left edge. That reached the world as a walkable edge with
/// left=0 and right=-920. The fuzzer found it inside a minute of the guard being written,
/// which is rather the point of having one.
bool to_length(std::string_view text, int& out) noexcept
{
    int value = 0;
    if (!to_coordinate(text, value) || value < 0) {
        return false;
    }
    out = value;
    return true;
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

    // An empty report, not `v 1`. This publishes the bootstrap floor and nothing else; the
    // version line used to be spelled out here, which meant the day format_version became 2
    // this call would announce a script speaking 1 -- before any script had said a word --
    // and print the whole "reinstall it" warning against itself.
    apply("");
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

    // Gathered rather than applied as they are read, because the list they belong to is not
    // this thread's to hold open across a parse -- see the merge at the bottom. Views into
    // `report`, which outlives this function, so nothing is copied to hold a name.
    std::vector<std::pair<std::string_view, PixelRect>> work_areas;

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
                    log_line("which KWin prefers over the packaged one. Delete it:");
                    log_line("    rm -rf ~/.local/share/kwin/scripts/dragonperch-geometry");
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
            if (!to_coordinate(fields.at[2], x) || !to_coordinate(fields.at[3], y)
                || !to_length(fields.at[4], width) || !to_length(fields.at[5], height)) {
                continue;
            }

            work_areas.emplace_back(fields.at[1], PixelRect{x, y, width, height});
            continue;
        }

        if (fields.at[0] == "w" && fields.count == 8) {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            int z = 0;
            int kind = 0;
            if (!to_coordinate(fields.at[2], x) || !to_coordinate(fields.at[3], y)
                || !to_length(fields.at[4], width) || !to_length(fields.at[5], height)
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

    WorldSnapshot snapshot;
    {
        const std::lock_guard lock{mutex_};

        // Merged into the list as it is now, rather than into a copy taken before the parse
        // and written back over the top of it. set_outputs runs on the Wayland thread while
        // this runs on the session bus worker, so a monitor plugged in while a report was
        // being read used to be discarded -- and nothing said so, because the next report
        // arrived a sixtieth of a second later and looked perfectly healthy without it. The
        // screen was simply missing its floor until some later output event put it back.
        //
        // Matched by connector name -- "DP-1", "eDP-1" -- which is what wl_output.name
        // reports on one side and what KWin calls the screen on the other. There is no other
        // identifier the two halves share.
        for (const auto& [name, area] : work_areas) {
            const auto it = std::ranges::find(outputs_, name, &OutputInfo::name);
            if (it != outputs_.end()) {
                it->work_area = area;
            }
        }

        for (const OutputInfo& output : outputs_) {
            // A screen with nothing usable on it has no floor. The parser will not accept a
            // negative width any more, but the invariant belongs here as well: this is the
            // one place that turned a rectangle into an edge without ever looking at it,
            // which is how a rectangle that was not one came out of it as an edge whose
            // right was to the left of its left.
            if (output.work_area.empty()) {
                continue;
            }

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

        snapshot_ = WorldSnapshot{++version_, std::move(edges), outputs_};
        snapshot = snapshot_;
    }

    handler_.call(snapshot);
}

} // namespace dp::wl
