// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.hpp"

#include <cstdio>
#include <string>

namespace dp::wl {

void log_line(std::string_view text)
{
    // Assembled and written in one call rather than three. stdio locks the stream per call,
    // so text-then-newline lets another thread's line land in between and leaves two lines
    // spliced together -- and this daemon has a second thread that logs: SessionBus::run()
    // reports bus failures from its own worker while the main thread reports everything else.
    std::string line;
    line.reserve(text.size() + 1);
    line.append(text);
    line.push_back('\n');

    // Discarded deliberately, and cast to say so. This is the function that failures are
    // reported through, so a failure of it has nowhere to be reported to. stderr is
    // unbuffered, but the flush stays for the case where somebody has redirected it.
    static_cast<void>(std::fwrite(line.data(), 1, line.size(), stderr));
    static_cast<void>(std::fflush(stderr));
}

} // namespace dp::wl
