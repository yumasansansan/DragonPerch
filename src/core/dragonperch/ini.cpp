// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/ini.hpp"

#include "dragonperch/text.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace dp::ini {
namespace {

constexpr std::string_view whitespace = " \t\r\n";

} // namespace

std::string_view trim(std::string_view text) noexcept
{
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

const Entry* Section::find(std::string_view key) const noexcept
{
    // The last entry with that key, not the first. Neither settings program writes a file
    // that repeats one, but a person editing by hand does -- and what somebody means by
    // putting a corrected line under the old one is the correction. It is what INI
    // conventionally means, what KConfig does, and what the Windows settings program has
    // always done with this same file, by the simple route of assigning as it reads.
    const auto reversed = entries | std::views::reverse;
    const auto it = std::ranges::find(reversed, key, [](const Entry& entry) {
        return std::string_view{entry.key};
    });
    return it == reversed.end() ? nullptr : &*it;
}

std::vector<Section> parse(std::string_view text, OnBadLine bad_line)
{
    std::vector<Section> sections;
    std::size_t line_number = 0;
    std::size_t start = 0;

    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string_view raw =
            text.substr(start, newline == std::string_view::npos ? std::string_view::npos
                                                                 : newline - start);
        ++line_number;

        std::string_view line = trim(raw);

        // Both comment markers: '#' is what most people type, ';' is what KConfig writes.
        if (const std::size_t comment = line.find_first_of("#;");
            comment != std::string_view::npos) {
            line = trim(line.substr(0, comment));
        }

        const bool refusing = bad_line == OnBadLine::refuse;

        if (line.empty()) {
            // Nothing on it but whitespace or a comment.
        } else if (line.front() == '[') {
            if (line.back() == ']') {
                sections.push_back(
                    Section{std::string{trim(line.substr(1, line.size() - 2))}, line_number, {}});
            } else if (refusing) {
                throw std::runtime_error(
                    cat("line ", line_number, ": unterminated section header"));
            } else {
                // Skipped -- but it still ends the section above it, rather than being
                // passed over as though it were a blank line. Keys written underneath a
                // header nobody could read were not meant for whatever section came before
                // it, and quietly filing them there would be a worse answer than losing
                // them. They go into a section with no name instead, which no lookup asks
                // for. The Windows settings program reaches the same place by a different
                // route: it simply stops being in a section.
                sections.push_back(Section{std::string{}, line_number, {}});
            }
        } else {
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos) {
                if (refusing) {
                    throw std::runtime_error(
                        cat("line ", line_number, ": expected 'key = value', got '", line, "'"));
                }
            } else if (sections.empty()) {
                if (refusing) {
                    throw std::runtime_error(
                        cat("line ", line_number, ": key outside any section"));
                }
            } else {
                sections.back().entries.push_back(Entry{
                    std::string{trim(line.substr(0, equals))},
                    std::string{trim(line.substr(equals + 1))},
                    line_number,
                });
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }

    return sections;
}

const Entry* find(const std::vector<Section>& sections, std::string_view section,
                  std::string_view key) noexcept
{
    const auto it = std::ranges::find(sections, section, &Section::name);
    return it == sections.end() ? nullptr : it->find(key);
}

} // namespace dp::ini
