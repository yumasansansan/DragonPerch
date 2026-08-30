// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/settings.hpp"

#include "dragonperch/ini.hpp"
#include "dragonperch/text.hpp"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <system_error>

namespace dp {
namespace {

constexpr std::string_view section_name = "DragonPerch";

/// One value, or the default. Nothing here throws: see parse_settings.
void read_int(const ini::Section& section, std::string_view key, int& out, int lowest,
              int highest)
{
    const ini::Entry* entry = section.find(key);
    if (entry == nullptr) {
        return;
    }

    int value = 0;
    const char* first = entry->value.data();
    const char* last = first + entry->value.size();
    if (const auto [ptr, error] = std::from_chars(first, last, value);
        error == std::errc{} && ptr == last) {
        out = std::clamp(value, lowest, highest);
    }
}

/// strtod rather than std::from_chars, and that is not a style choice.
///
/// from_chars on a double is what drags in a hundred and eighteen kilobytes of Ryu
/// conversion tables -- the thing dragonperch/text.hpp exists to keep out, and half the
/// binary when it was last measured. strtod lives in the C runtime and costs an import.
///
/// It reads the decimal point from the C locale, which nothing here ever sets, so it is
/// the "C" locale and the point is a full stop.
void read_double(const ini::Section& section, std::string_view key, double& out, double lowest,
                 double highest)
{
    const ini::Entry* entry = section.find(key);
    if (entry == nullptr || entry->value.empty()) {
        return;
    }

    char* end = nullptr;
    const double value = std::strtod(entry->value.c_str(), &end);

    // isfinite, because strtod accepts "nan" and "inf" and std::clamp passes a NaN
    // straight through -- both comparisons inside it are false against a NaN, so it
    // returns the NaN. That reaches std::lround in the simulation, where it is undefined
    // behaviour rather than a slow dragon. "inf" is less exciting and just as wrong: it
    // clamps to the maximum, so the file says one thing and the pets do another.
    if (end != nullptr && *end == 0 && std::isfinite(value)) {
        out = std::clamp(value, lowest, highest);
    }
}

void read_bool(const ini::Section& section, std::string_view key, bool& out)
{
    const ini::Entry* entry = section.find(key);
    if (entry == nullptr) {
        return;
    }

    // What KConfig writes is "true"/"false"; what people type is anybody's guess.
    const std::string& value = entry->value;
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        out = true;
    } else if (value == "false" || value == "0" || value == "no" || value == "off") {
        out = false;
    }
}

void read_list(const ini::Section& section, std::string_view key, std::vector<std::string>& out)
{
    const ini::Entry* entry = section.find(key);
    if (entry == nullptr) {
        return;
    }

    out.clear();

    const std::string_view text = entry->value;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string_view item = ini::trim(
            text.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                               : comma - start));
        if (!item.empty()) {
            out.emplace_back(item);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
}

std::string join(const std::vector<std::string>& items)
{
    std::string out;
    for (const std::string& item : items) {
        if (!out.empty()) {
            out += ", ";
        }
        out += item;
    }
    return out;
}

/// Two decimal places, without `<format>` and without dragging in the floating-point
/// conversion tables it would bring with it. See dragonperch/text.hpp.
std::string two_places(double value)
{
    const bool negative = value < 0.0;
    const double magnitude = negative ? -value : value;

    // llround, not (x + 0.5) truncated. The two agree almost everywhere and disagree
    // exactly where it is hardest to notice: adding 0.5 to a double just below a .5
    // boundary can round *up* in the addition itself, so the truncation then lands a
    // whole unit high. clang-tidy's bugprone-incorrect-roundings is what pointed at this.
    const long long scaled = std::llround(magnitude * 100.0);
    const long long whole = scaled / 100;
    const long long fraction = scaled % 100;

    return cat(negative ? "-" : "", whole, ".", fraction < 10 ? "0" : "", fraction);
}

bool contains(const std::vector<std::string>& items, std::string_view value)
{
    return std::ranges::find(items, value) != items.end();
}

} // namespace

SimulationOptions Settings::to_options() const noexcept
{
    SimulationOptions options;
    options.walk_speed = walk_speed;
    options.idle_interval = Duration{idle_interval};
    return options;
}

bool Settings::needs_respawn(const Settings& other) const noexcept
{
    return pets_per_mascot != other.pets_per_mascot || mascots != other.mascots
           || outputs != other.outputs;
}

bool Settings::wants_mascot(std::string_view id) const noexcept
{
    return mascots.empty() || contains(mascots, id);
}

bool Settings::wants_output(std::string_view name) const noexcept
{
    return outputs.empty() || contains(outputs, name);
}

Settings parse_settings(std::string_view text)
{
    Settings settings;

    std::vector<ini::Section> sections;
    try {
        // Line by line, not all or nothing. This file is edited by hand and written by two
        // other programs, and one typo should cost the setting it is in rather than every
        // setting in the file -- which is what the file's own header comment promises the
        // person editing it, and what the Windows settings program has always done with the
        // same file. Until now the daemon read it the other way: a single unreadable line
        // put every setting back to its default, so the two programs could disagree about
        // what the user's settings were and neither said anything.
        sections = ini::parse(text, ini::OnBadLine::skip);
    } catch (const std::exception&) {
        // Nothing in skip mode throws by design, so what is left is bad_alloc on a file
        // large enough to matter. parse_settings promises never to throw, and the fuzz
        // target holds it to that promise rather than taking it on trust.
        return settings;
    }

    // Every section with the name, in the order they appear, rather than the first one and
    // no further. A hand-edited file opens the same section twice easily enough, and what a
    // person means by writing the header again is that reading should carry on. Applied in
    // order, so a question answered twice takes the later answer -- the same rule as within
    // a section, and the same rule the Windows settings program follows.
    for (const ini::Section& section : sections) {
        if (section.name != section_name) {
            continue;
        }

        read_int(section, "pets-per-mascot", settings.pets_per_mascot, 0, 64);
        read_list(section, "mascots", settings.mascots);
        read_double(section, "walk-speed", settings.walk_speed, 1.0, 1000.0);
        read_double(section, "idle-interval", settings.idle_interval, 0.0, 3600.0);
        read_list(section, "outputs", settings.outputs);
        read_bool(section, "pause-for-fullscreen", settings.pause_for_fullscreen);
    }

    return settings;
}

std::string write_settings(const Settings& settings)
{
    return cat(
        "; DragonPerch settings. Written by the settings program and safe to edit by hand;\n"
        "; a line that cannot be read is ignored and its setting keeps the default.\n"
        "\n[",
        section_name,
        "]\n"
        "\n; How many of each mascot. Three mascots at 2 is six dragons.\n"
        "pets-per-mascot = ",
        settings.pets_per_mascot,
        "\n"
        "\n; Which mascots, by pack id: konqi, katie, kori. Empty means all of them.\n"
        "mascots = ",
        join(settings.mascots),
        "\n"
        "\n; Pixels per second. The walk cycle was drawn against 42; much faster and the\n"
        "; feet slide, much slower and it moonwalks.\n"
        "walk-speed = ",
        two_places(settings.walk_speed),
        "\n"
        "\n; Mean seconds between spontaneous pauses. 0 stops them happening.\n"
        "idle-interval = ",
        two_places(settings.idle_interval),
        "\n"
        "\n; Which monitors, by the name the system reports. Empty means all of them.\n"
        "outputs = ",
        join(settings.outputs),
        "\n"
        "\n; Hide the pets on a monitor showing something full screen.\n"
        "pause-for-fullscreen = ",
        settings.pause_for_fullscreen ? "true" : "false", "\n");
}

} // namespace dp
