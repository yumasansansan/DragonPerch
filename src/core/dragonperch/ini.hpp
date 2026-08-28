// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace dp::ini {

/// INI, because two separate things in this program want it and one of them settled the
/// argument: the Linux settings program is a KDE Config Module, KConfig reads and writes
/// INI, and a settings file it cannot open would be a settings program that has to bring
/// its own parser.
///
/// Sprite packs were here first and had their own copy of this. One parser now, so a file
/// that reads one way in a pack cannot read another way in the settings.
///
/// Deliberately small: sections, keys, values, and comments. No quoting, no escapes, no
/// nesting, no types. What is on the right of the `=` is a string until somebody asks it
/// to be something else.

/// One `key = value`, with the line it came from so a complaint can name it.
struct Entry {
    std::string key;
    std::string value;
    std::size_t line = 0;
};

/// One `[section]` and its keys, in the order they were read.
struct Section {
    std::string name;
    std::size_t line = 0;
    std::vector<Entry> entries;

    [[nodiscard]] const Entry* find(std::string_view key) const noexcept;
};

/// Splits text into sections. Throws only on a malformed section header or a line outside
/// any section -- everything else a caller has to judge for itself, because what counts as
/// valid depends on what the file is for.
///
/// Both `#` and `;` start a comment: the first is what people type, the second is what
/// KConfig writes.
[[nodiscard]] std::vector<Section> parse(std::string_view text);

/// The value of `key` in `[section]`, or nothing.
[[nodiscard]] const Entry* find(const std::vector<Section>& sections, std::string_view section,
                                std::string_view key) noexcept;

/// Whitespace off both ends. Exposed because callers that split a value on commas need the
/// same rule the parser used, and two rules would differ eventually.
[[nodiscard]] std::string_view trim(std::string_view text) noexcept;

} // namespace dp::ini
