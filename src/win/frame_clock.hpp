// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/render.hpp"

#include <chrono>

namespace dp::win {

/// Paces the loop off DWM composition rather than a timer.
///
/// DwmFlush blocks until DWM finishes its next present, which self-throttles to the refresh
/// rate: the Windows counterpart to Wayland's wl_surface.frame. A timer would drift against
/// the compositor and produce visible judder at no benefit.
class DwmFrameClock final : public IFrameClock {
public:
    std::chrono::nanoseconds wait_for_next_frame() override;

private:
    std::chrono::steady_clock::time_point last_{};
};

} // namespace dp::win
