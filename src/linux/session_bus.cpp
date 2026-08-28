// SPDX-License-Identifier: GPL-3.0-or-later
#include "session_bus.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"

#include <cstring>
#include <stdexcept>

#include <systemd/sd-bus.h>

namespace dp::wl {

SessionBus::~SessionBus()
{
    stop();
}

void SessionBus::open()
{
    if (const int failed = sd_bus_open_user(&bus_); failed < 0) {
        throw std::runtime_error(cat("cannot reach the session bus: ", std::strerror(-failed)));
    }
}

void SessionBus::request_name(const char* name)
{
    if (const int failed = sd_bus_request_name(bus_, name, 0); failed < 0) {
        throw std::runtime_error(cat("cannot claim ", name,
                                     " -- another DragonPerch is probably already running: ",
                                     std::strerror(-failed)));
    }
}

void SessionBus::add_object(const char* path, const char* interface, const sd_bus_vtable* vtable,
                            void* userdata)
{
    sd_bus_slot* slot = nullptr;
    if (const int failed = sd_bus_add_object_vtable(bus_, &slot, path, interface, vtable, userdata);
        failed < 0) {
        throw std::runtime_error(cat("cannot publish ", path, ": ", std::strerror(-failed)));
    }
    slots_.push_back(slot);
}

void SessionBus::start()
{
    if (started_) {
        return;
    }
    started_ = true;
    worker_ = std::thread{[this] { run(); }};
}

void SessionBus::run()
{
    while (!stopping_.load(std::memory_order_relaxed)) {
        const int processed = sd_bus_process(bus_, nullptr);
        if (processed < 0) {
            log_line(cat("session bus error: ", std::strerror(-processed)));
            return;
        }
        if (processed > 0) {
            // More may be queued behind it; go round again without waiting.
            continue;
        }

        // A timeout rather than an indefinite wait, so that stopping does not depend on
        // somebody sending one last message to wake this thread up.
        if (const int failed = sd_bus_wait(bus_, 200000); failed < 0) {
            log_line(cat("session bus wait failed: ", std::strerror(-failed)));
            return;
        }
    }
}

void SessionBus::stop() noexcept
{
    stopping_.store(true, std::memory_order_relaxed);
    if (worker_.joinable()) {
        worker_.join();
    }

    for (sd_bus_slot* slot : slots_) {
        sd_bus_slot_unref(slot);
    }
    slots_.clear();

    if (bus_ != nullptr) {
        sd_bus_unref(bus_);
        bus_ = nullptr;
    }
}

} // namespace dp::wl
