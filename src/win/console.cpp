// SPDX-License-Identifier: GPL-3.0-or-later
#include "console.hpp"

#include "win_headers.hpp"

#include <cstdio>
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

} // namespace dp::win
