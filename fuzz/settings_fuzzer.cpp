// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragonperch/settings.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

/// The settings file, which is hand-edited and written by two other programs.
///
/// Nothing is caught here, and that is the point. parse_settings promises never to throw --
/// a file with a stray bracket has to come back as the defaults rather than stop the pets
/// appearing -- so an exception escaping this function is a real finding, and libFuzzer
/// will report it as one.
///
/// The assertions below are the other half of the contract. A value that reaches the
/// simulation outside its stated range, or is not a number at all, is how a NaN walk speed
/// got as far as std::lround once already.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    const dp::Settings settings = dp::parse_settings(text);

    const bool sane = settings.pets_per_mascot >= 0 && settings.pets_per_mascot <= 64
                      && std::isfinite(settings.walk_speed) && settings.walk_speed >= 1.0
                      && settings.walk_speed <= 1000.0 && std::isfinite(settings.idle_interval)
                      && settings.idle_interval >= 0.0 && settings.idle_interval <= 3600.0;
    if (!sane) {
        std::abort();
    }

    // And the round trip, because what this program writes has to be something it can read
    // back: the settings window saves the whole file every time, so a value that survives
    // parsing but is written out unreadably would lose the lot on the next open.
    const dp::Settings again = dp::parse_settings(dp::write_settings(settings));
    if (again.pets_per_mascot != settings.pets_per_mascot || again.mascots != settings.mascots
        || again.outputs != settings.outputs
        || again.pause_for_fullscreen != settings.pause_for_fullscreen) {
        std::abort();
    }

    // walk-speed and idle-interval are left out above because they cannot be in it: they
    // are written to two decimal places, so a value carrying more than that is allowed to
    // come back rounded. Which is exactly why they need checking here instead -- they are
    // the only two settings that pass through a lossy conversion, and the one real bug this
    // file's format has had so far was in that conversion.
    //
    // The property that does hold is that the rounding settles. One more pass has to change
    // nothing at all, doubles included, and this compares the whole struct to say so.
    const dp::Settings settled = dp::parse_settings(dp::write_settings(again));
    if (!(settled == again)) {
        std::abort();
    }

    return 0;
}
