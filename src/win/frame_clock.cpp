// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_clock.hpp"

#include "win_headers.hpp"

namespace dp::win {

std::chrono::nanoseconds DwmFrameClock::wait_for_next_frame()
{
    DwmFlush();

    const auto now = std::chrono::steady_clock::now();
    if (last_ == std::chrono::steady_clock::time_point{}) {
        last_ = now;
        return std::chrono::nanoseconds::zero();
    }

    const auto delta = now - last_;
    last_ = now;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(delta);
}

} // namespace dp::win
