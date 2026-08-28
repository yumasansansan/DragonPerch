// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/ini.hpp"

#include "dragonperch/text.hpp"

#include <algorithm>
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
    const auto it = std::ranges::find(entries, key, [](const Entry& entry) {
        return std::string_view{entry.key};
    });
    return it == entries.end() ? nullptr : &*it;
}

std::vector<Section> parse(std::string_view text)
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

        if (!line.empty()) {
            if (line.front() == '[') {
                if (line.back() != ']') {
                    throw std::runtime_error(
                        cat("line ", line_number, ": unterminated section header"));
                }
                sections.push_back(
                    Section{std::string{trim(line.substr(1, line.size() - 2))}, line_number, {}});
            } else {
                const std::size_t equals = line.find('=');
                if (equals == std::string_view::npos) {
                    throw std::runtime_error(
                        cat("line ", line_number, ": expected 'key = value', got '", line, "'"));
                }
                if (sections.empty()) {
                    throw std::runtime_error(
                        cat("line ", line_number, ": key outside any section"));
                }

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
