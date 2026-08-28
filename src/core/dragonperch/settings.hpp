// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/simulation.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace dp {

/// What a person is allowed to change, and nothing else.
///
/// Kept apart from SimulationOptions on purpose. That struct is the physics' own
/// vocabulary and has knobs in it -- terminal velocity, the chance of turning at an edge --
/// that exist to be tuned by whoever is writing the simulation, not by whoever is running
/// it. A settings file that exposed all of them would be a settings file nobody could give
/// a sensible answer to.
struct Settings {
    /// How many of each mascot. Six pets from three mascots is two each, which is what
    /// "how many dragons" means to somebody who has not read the source.
    int pets_per_mascot = 1;

    /// Which mascots, by pack id. Empty means every one that is installed, which is what
    /// somebody who has never opened the settings should get.
    std::vector<std::string> mascots;

    /// Pixels per second. The default is the one the walk cycle was drawn against: much
    /// faster and the feet slide, much slower and it moonwalks.
    double walk_speed = 42.0;

    /// Mean seconds between spontaneous pauses. Zero stops them happening at all.
    double idle_interval = 9.0;

    /// Which monitors, by the name the backend reports -- "DP-1" on Wayland, the device
    /// name on Windows. Empty means all of them.
    std::vector<std::string> outputs;

    /// Hide the pets on a monitor showing something full-screen. Implemented on Windows;
    /// milestone 11 on Wayland, where only the compositor can see it.
    bool pause_for_fullscreen = true;

    friend bool operator==(const Settings&, const Settings&) = default;

    /// The physics knobs this is allowed to reach.
    [[nodiscard]] SimulationOptions to_options() const noexcept;

    /// True when `other` would need the pets spawned again rather than just adjusted --
    /// changing the speed does not disturb anybody, changing which mascots there are does.
    [[nodiscard]] bool needs_respawn(const Settings& other) const noexcept;

    /// Is this mascot wanted?
    [[nodiscard]] bool wants_mascot(std::string_view id) const noexcept;

    /// Is this monitor wanted?
    [[nodiscard]] bool wants_output(std::string_view name) const noexcept;
};

/// Reads settings from INI. Unknown keys and unreadable values are ignored rather than
/// refused: a settings file is edited by hand and by two other programs, and losing every
/// setting because one line is wrong is worse than losing the one line. What is missing
/// keeps its default.
[[nodiscard]] Settings parse_settings(std::string_view text);

/// Writes settings as INI, with a comment saying what each one does.
///
/// The whole file every time, including the defaults. A settings file that only lists what
/// has been changed is one nobody can read to find out what is changeable.
[[nodiscard]] std::string write_settings(const Settings& settings);

} // namespace dp
