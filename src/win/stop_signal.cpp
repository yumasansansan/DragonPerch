// SPDX-License-Identifier: GPL-3.0-or-later
#include "stop_signal.hpp"

#include "win_headers.hpp"

namespace dp::win {
namespace {

constexpr const wchar_t* event_name = L"Local\\DragonPerch.Stop";

HANDLE g_event = nullptr;

} // namespace

void create_stop_signal()
{
    // Manual reset, so that once it is raised every part of the program that looks sees it,
    // rather than the first reader consuming it.
    g_event = CreateEventW(nullptr, TRUE, FALSE, event_name);
}

bool stop_requested()
{
    return g_event != nullptr && WaitForSingleObject(g_event, 0) == WAIT_OBJECT_0;
}

bool raise_stop_signal()
{
    HANDLE running = OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name);
    if (running == nullptr) {
        return false;
    }

    const BOOL set = SetEvent(running);
    CloseHandle(running);
    return set != FALSE;
}

} // namespace dp::win
