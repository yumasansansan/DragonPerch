// SPDX-License-Identifier: GPL-3.0-or-later
#include "log.hpp"

#include <cstdio>

namespace dp::wl {

void log_line(std::string_view text)
{
    std::fwrite(text.data(), 1, text.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

} // namespace dp::wl
