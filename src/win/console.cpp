// SPDX-License-Identifier: GPL-3.0-or-later
#include "console.hpp"

#include "log.hpp"
#include "win_headers.hpp"

#include <cstdio>
#include <format>
#include <io.h>
#include <iostream>

namespace dp::win {

void attach_parent_console()
{
    if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
        return;
    }

    // The CRT has already bound stdout/stderr to nothing by the time main runs; rebind them
    // or every write is silently discarded.
    FILE* out = nullptr;
    FILE* err = nullptr;
    (void)freopen_s(&out, "CONOUT$", "w", stdout);
    (void)freopen_s(&err, "CONOUT$", "w", stderr);

    std::ios::sync_with_stdio(true);
    std::cout.clear();
    std::cerr.clear();
}

namespace {

void (*g_on_stop)() = nullptr;

BOOL WINAPI console_handler(DWORD event)
{
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        log_line(std::format("stopping: console event {}", event));
        if (g_on_stop != nullptr) {
            g_on_stop();
        }
        // TRUE: handled, so the default handler does not terminate the process from under
        // the render loop. The loop sees the flag and unwinds properly, which is what gets
        // the DirectComposition device and the overlay windows torn down.
        return TRUE;
    default:
        return FALSE;
    }
}

} // namespace

void handle_console_stop(void (*on_stop)())
{
    g_on_stop = on_stop;

    // Registering succeeds even with no console attached, and then nothing is ever
    // delivered -- which is indistinguishable from a hung loop. Worth a line.
    if (SetConsoleCtrlHandler(&console_handler, TRUE) == FALSE || GetConsoleWindow() == nullptr) {
        log_line("no console to listen to; Ctrl+C will not stop this");
    }
}

} // namespace dp::win
