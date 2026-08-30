// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/handler_slot.hpp"
#include "dragonperch/world.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

struct sd_bus_message;
struct sd_bus_error;

namespace dp::wl {

class SessionBus;

/// Where the windows are, according to KWin.
///
/// A Wayland client cannot see another client's windows. That is not an oversight to route
/// around -- it is the protocol working as designed -- so the only honest answer on Plasma
/// is to ask the compositor, and the only way to ask is to run a script inside it. See
/// `kwin/dragonperch-geometry/`, which pushes a line of text per window over the session
/// bus every time the desktop changes.
///
/// This is the receiving end: it publishes `/org/dragonperch/Geometry` on the session bus
/// and turns each report into a WorldSnapshot. All of the judgement lives here rather than
/// in the script, because the script runs on KWin's main thread and anything slow there is
/// session-wide jank.
///
/// The connection and the thread belong to SessionBus, not here. The control interface
/// publishes on the same one, and a provider that owned the bus would have had to lend it
/// out.
class KWinGeometryProvider final : public IWorldProvider {
public:
    KWinGeometryProvider();
    ~KWinGeometryProvider() override;

    KWinGeometryProvider(const KWinGeometryProvider&) = delete;
    KWinGeometryProvider& operator=(const KWinGeometryProvider&) = delete;
    KWinGeometryProvider(KWinGeometryProvider&&) = delete;
    KWinGeometryProvider& operator=(KWinGeometryProvider&&) = delete;

    /// The monitors, as the Wayland side found them. Their bounds are used as they are; the
    /// usable area is filled in from KWin's report, which is the only side that knows where
    /// the panels have taken their struts.
    void set_outputs(std::span<const OutputInfo> outputs);

    [[nodiscard]] WorldSnapshot current() const override;
    void set_changed_handler(ChangedHandler handler) override;

    /// Registers the object. Call before the bus starts processing, so that the first
    /// report cannot arrive before there is anything to receive it.
    void publish(SessionBus& bus);

    /// Publishes what is known before KWin has said anything: the outputs, and a floor on
    /// each. A pet spawned first then lands on the bottom of the screen rather than falling
    /// for ever.
    ///
    /// Idempotent, which the IWorldProvider contract requires: PetHost calls it too.
    void start() override;

    /// Prints every report from KWin exactly as it arrived, before anything is made of it.
    ///
    /// Which line is missing, and whether it was ever sent, is the difference between a bug
    /// in the script and a bug in the parsing -- and guessing between the two has already
    /// cost two rounds of "try this and tell me what happens".
    void log_raw_reports(bool on) { log_raw_ = on; }

    /// True once at least one report has arrived. Until then there is nothing to stand on
    /// but the floor, which usually means the script is not installed or not enabled.
    [[nodiscard]] bool heard_from_kwin() const noexcept { return reports_ > 0; }

    /// Turns one report into a snapshot.
    ///
    /// Public because it is the parsing, and the parsing is the part worth reaching from
    /// outside: it takes text written by another process and turns it into the geometry
    /// the pets walk on. fuzz/kwin_report_fuzzer.cpp drives exactly this. Nothing about it
    /// needs the bus -- on_update below only unwraps the message and hands the string over.
    void apply(std::string_view report);

private:
    static int on_update(sd_bus_message* message, void* userdata, sd_bus_error* error);

    bool started_ = false;
    bool log_raw_ = false;
    std::atomic<std::uint64_t> reports_{0};

    /// `current()` is read from the render loop while the bus thread publishes.
    mutable std::mutex mutex_;
    WorldSnapshot snapshot_;
    std::vector<OutputInfo> outputs_;
    std::uint64_t version_ = 0;

    HandlerSlot handler_;
};

} // namespace dp::wl
