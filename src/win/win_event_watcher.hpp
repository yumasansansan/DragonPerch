// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/handler_slot.hpp"
#include "dragonperch/world.hpp"
#include "win_headers.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace dp::win {

/// Watches the Win32 desktop and republishes it as WorldSnapshots.
///
/// Event driven, never polling. Enumerating every top-level window at 60 Hz would show up
/// in the user's battery life, and a desktop pet that costs measurable power is a desktop
/// pet that gets uninstalled.
///
/// `SetWinEventHook` with `WINEVENT_OUTOFCONTEXT` delivers through the calling thread's
/// message queue, so this owns a dedicated thread running a real message pump. That is also
/// why the hook callback does no work beyond raising a flag: it runs on the delivery queue,
/// and stalling it stalls the window being dragged.
class WinEventWatcher final : public IWorldProvider {
public:
    WinEventWatcher();
    WinEventWatcher(const WinEventWatcher&) = delete;
    WinEventWatcher& operator=(const WinEventWatcher&) = delete;
    WinEventWatcher(WinEventWatcher&&) = delete;
    WinEventWatcher& operator=(WinEventWatcher&&) = delete;
    ~WinEventWatcher() override;

    [[nodiscard]] WorldSnapshot current() const override;
    void set_changed_handler(ChangedHandler handler) override;

    /// Idempotent: a head legitimately needs the first snapshot before it can build its
    /// overlays, since it has to know the monitor layout, so it starts this itself and then
    /// hands it to the host loop, which starts it again.
    void start() override;

    /// Raw hook callbacks accepted, and snapshots published. Diagnostic only: it separates
    /// "the hook is not delivering" from "the coalescing is swallowing everything", which
    /// look identical from the outside.
    [[nodiscard]] std::uint64_t events_seen() const noexcept
    {
        return events_seen_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t events_filtered() const noexcept
    {
        return events_filtered_.load(std::memory_order_relaxed);
    }

private:
    void pump_messages();
    void publish_loop();
    void publish();
    void mark_dirty();

    // Signature fixed by WINEVENTPROC. Windows types rather than plain integers, because
    // the function pointer has to match exactly for SetWinEventHook to accept it.
    static void CALLBACK on_win_event(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object_id,
                                      LONG child_id, DWORD thread_id, DWORD time_ms);

    /// Dragging a window emits a location change per compositor frame. The simulation reads
    /// one snapshot per frame, so anything finer is wasted work.
    static constexpr std::chrono::milliseconds coalesce_interval{16};

    mutable std::mutex snapshot_mutex_;
    WorldSnapshot current_;
    HandlerSlot changed_;
    std::uint64_t version_ = 0;

    std::mutex dirty_mutex_;
    std::condition_variable dirty_signal_;
    bool dirty_ = false;

    std::atomic<std::uint64_t> events_seen_{0};
    std::atomic<std::uint64_t> events_filtered_{0};

    std::atomic<bool> stopping_{false};
    std::atomic<DWORD> pump_thread_id_{0};
    bool started_ = false;

    std::thread pump_thread_;
    std::thread publish_thread_;
};

} // namespace dp::win
