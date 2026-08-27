// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/world.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

struct sd_bus;
struct sd_bus_message;
struct sd_bus_slot;
struct sd_bus_error;

namespace dp::wl {

/// Where the windows are, according to KWin.
///
/// A Wayland client cannot see another client's windows. That is not an oversight to route
/// around -- it is the protocol working as designed -- so the only honest answer on Plasma
/// is to ask the compositor, and the only way to ask is to run a script inside it. See
/// `kwin/dragonperch-geometry/`, which pushes a line of text per window over the session
/// bus every time the desktop changes.
///
/// This owns the receiving end: it claims `org.dragonperch.Geometry` on the session bus and
/// turns each report into a WorldSnapshot. All of the judgement lives here rather than in
/// the script, because the script runs on KWin's main thread and anything slow there is
/// session-wide jank.
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

    [[nodiscard]] const WorldSnapshot& current() const override;
    void set_changed_handler(ChangedHandler handler) override;

    /// Claims the bus name and starts listening on its own thread. Throws if the session
    /// bus is unreachable, or if something else already owns the name.
    void start() override;

    void stop() noexcept;

    /// True once at least one report has arrived. Until then there is nothing to stand on
    /// but the floor, which usually means the script is not installed or not enabled.
    [[nodiscard]] bool heard_from_kwin() const noexcept { return reports_ > 0; }

private:
    static int on_update(sd_bus_message* message, void* userdata, sd_bus_error* error);

    void apply(std::string_view report);
    void run();

    sd_bus* bus_ = nullptr;
    sd_bus_slot* slot_ = nullptr;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint64_t> reports_{0};

    /// `current()` is read from the render loop while the bus thread publishes.
    mutable std::mutex mutex_;
    WorldSnapshot snapshot_;
    std::vector<OutputInfo> outputs_;
    std::uint64_t version_ = 0;

    ChangedHandler handler_;
};

} // namespace dp::wl
