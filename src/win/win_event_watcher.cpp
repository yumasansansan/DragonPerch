// SPDX-License-Identifier: GPL-3.0-or-later
#include "win_event_watcher.hpp"

#include "desktop_scanner.hpp"
#include "log.hpp"
#include "win_headers.hpp"

#include <array>
#include <utility>

namespace dp::win {
namespace {

/// The one instance the unmanaged callback can reach.
///
/// `SetWinEventHook` has no user-data parameter, so the callback cannot be given a `this`.
/// A single watcher is all the app ever wants, and pretending otherwise would mean a
/// registry keyed on something the callback does not receive either.
std::atomic<WinEventWatcher*> g_watcher{nullptr};

/// Two ranges rather than one span covering both. Everything between them is
/// accessibility traffic -- caret moves, focus changes inside controls, name changes --
/// which arrives constantly and is of no use here.
constexpr std::array<std::pair<UINT, UINT>, 2> hook_ranges{{
    {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_MINIMIZEEND},
    {EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE},
}};

} // namespace

WinEventWatcher::WinEventWatcher() = default;

WinEventWatcher::~WinEventWatcher()
{
    stopping_.store(true, std::memory_order_relaxed);

    // Wake the pump so it can unhook and leave.
    if (const DWORD id = pump_thread_id_.load(std::memory_order_relaxed); id != 0) {
        PostThreadMessageW(id, WM_QUIT, 0, 0);
    }

    {
        const std::lock_guard lock(dirty_mutex_);
        dirty_ = true;
    }
    dirty_signal_.notify_all();

    if (publish_thread_.joinable()) {
        publish_thread_.join();
    }
    if (pump_thread_.joinable()) {
        pump_thread_.join();
    }

    g_watcher.store(nullptr, std::memory_order_release);
}

WorldSnapshot WinEventWatcher::current() const
{
    const std::lock_guard lock(snapshot_mutex_);
    return current_;
}

void WinEventWatcher::set_changed_handler(ChangedHandler handler)
{
    const std::lock_guard lock(snapshot_mutex_);
    changed_ = std::move(handler);
}

void WinEventWatcher::start()
{
    if (started_) {
        return;
    }
    started_ = true;

    g_watcher.store(this, std::memory_order_release);

    pump_thread_ = std::thread(&WinEventWatcher::pump_messages, this);
    publish_thread_ = std::thread(&WinEventWatcher::publish_loop, this);

    // Prime the world, so the first frame is not empty and a caller can read the monitor
    // layout straight after start().
    publish();
}

void WinEventWatcher::pump_messages()
{
    pump_thread_id_.store(GetCurrentThreadId(), std::memory_order_relaxed);

    std::array<HWINEVENTHOOK, hook_ranges.size()> hooks{};
    for (std::size_t i = 0; i < hook_ranges.size(); ++i) {
        const auto [first, last] = hook_ranges[i];
        hooks[i] = SetWinEventHook(first, last, nullptr, &WinEventWatcher::on_win_event, 0, 0,
                                   WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (hooks[i] == nullptr) {
            log_line("dragonperch: SetWinEventHook failed; window tracking will be static");
        }
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    for (const HWINEVENTHOOK hook : hooks) {
        if (hook != nullptr) {
            UnhookWinEvent(hook);
        }
    }
}

void CALLBACK WinEventWatcher::on_win_event(HWINEVENTHOOK /*hook*/, DWORD /*event*/, HWND hwnd,
                                            LONG object_id, LONG child_id, DWORD /*thread_id*/,
                                            DWORD /*time_ms*/)
{
    WinEventWatcher* watcher = g_watcher.load(std::memory_order_acquire);
    if (watcher == nullptr) {
        return;
    }

    // Whole windows only. Without this filter every caret move and focus change inside
    // every application arrives here.
    if (object_id != OBJID_WINDOW || child_id != CHILDID_SELF || hwnd == nullptr) {
        watcher->events_filtered_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    watcher->events_seen_.fetch_add(1, std::memory_order_relaxed);
    {
        // Nothing but a flag. This runs on the hook delivery queue, and doing real work
        // here stalls the window being dragged.
        watcher->mark_dirty();
    }
}

void WinEventWatcher::mark_dirty()
{
    {
        const std::lock_guard lock(dirty_mutex_);
        dirty_ = true;
    }
    dirty_signal_.notify_one();
}

void WinEventWatcher::publish_loop()
{
    while (!stopping_.load(std::memory_order_relaxed)) {
        {
            std::unique_lock lock(dirty_mutex_);
            dirty_signal_.wait(lock, [this] {
                return dirty_ || stopping_.load(std::memory_order_relaxed);
            });
            dirty_ = false;
        }

        if (stopping_.load(std::memory_order_relaxed)) {
            return;
        }

        // Let the burst finish before scanning: one scan after a 16 ms drag storm rather
        // than one scan per event.
        std::this_thread::sleep_for(coalesce_interval);

        if (stopping_.load(std::memory_order_relaxed)) {
            return;
        }

        publish();
    }
}

void WinEventWatcher::publish()
{
    desktop_scanner::Scan scan = desktop_scanner::scan();

    ChangedHandler handler;
    WorldSnapshot snapshot{++version_, std::move(scan.edges), std::move(scan.outputs)};

    {
        const std::lock_guard lock(snapshot_mutex_);
        current_ = snapshot;
        handler = changed_;
    }

    // Outside the lock: the handler runs the simulation's world swap, and holding a mutex
    // across a callback into other code is how deadlocks are built.
    if (handler) {
        handler(snapshot);
    }
}

} // namespace dp::win
