// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <thread>
#include <vector>

struct sd_bus;
struct sd_bus_slot;
struct sd_bus_vtable;

namespace dp::wl {

/// One connection to the session bus, and the thread that drives it.
///
/// Two unrelated things live on it: KWin's geometry reports arrive here, and DragonPerch
/// can be told to pause or quit here. Both are D-Bus and neither is busy, so a second
/// connection and a second thread would buy nothing -- and having the geometry provider
/// own a bus that something else needed to publish on was the shape this replaced.
///
/// Names and objects are registered before `start`. After that the thread owns the
/// connection and nothing else may touch it: sd-bus is not thread-safe, and a blocking
/// call from another thread while this one is in `sd_bus_process` is a race.
class SessionBus {
public:
    SessionBus() = default;
    ~SessionBus();

    SessionBus(const SessionBus&) = delete;
    SessionBus& operator=(const SessionBus&) = delete;
    SessionBus(SessionBus&&) = delete;
    SessionBus& operator=(SessionBus&&) = delete;

    /// Connects. Throws when there is no session bus to connect to.
    void open();

    /// Claims a well-known name. Throws if something already holds it -- no replacing and
    /// no queueing, so a second DragonPerch fails loudly rather than quietly taking over
    /// and leaving the first one drawing a desktop that never changes again.
    void request_name(const char* name);

    /// Publishes an object. The vtable is read for as long as the connection lives, so it
    /// must outlive this call.
    void add_object(const char* path, const char* interface, const sd_bus_vtable* vtable,
                    void* userdata);

    /// Starts processing on its own thread. Idempotent.
    void start();
    void stop() noexcept;

    [[nodiscard]] bool connected() const noexcept { return bus_ != nullptr; }

private:
    void run();

    sd_bus* bus_ = nullptr;
    std::vector<sd_bus_slot*> slots_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    bool started_ = false;
};

} // namespace dp::wl
