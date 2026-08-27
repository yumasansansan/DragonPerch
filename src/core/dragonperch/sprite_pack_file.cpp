// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/sprite_pack_file.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dp {
namespace {

constexpr std::string_view whitespace = " \t\r\n";

std::string_view trim(std::string_view text)
{
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

[[noreturn]] void fail(std::size_t line, std::string_view what)
{
    throw std::runtime_error(std::format("sprite pack line {}: {}", line, what));
}

int to_int(std::string_view text, std::size_t line, std::string_view what)
{
    const std::string_view value = trim(text);
    int result = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec != std::errc{} || ptr != value.data() + value.size()) {
        fail(line, std::format("{} is not a number: '{}'", what, value));
    }
    return result;
}

std::vector<int> to_int_list(std::string_view text, std::size_t line, std::string_view what)
{
    std::vector<int> values;
    std::size_t start = 0;

    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string_view item =
            text.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                               : comma - start);
        if (!trim(item).empty()) {
            values.push_back(to_int(item, line, what));
        }

        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }

    if (values.empty()) {
        fail(line, std::format("{} is empty", what));
    }
    return values;
}

/// One `[section]` and its keys, in the order they were read.
struct Section {
    std::string name;
    std::size_t line = 0;
    std::vector<std::pair<std::string, std::pair<std::string, std::size_t>>> keys;

    [[nodiscard]] const std::pair<std::string, std::size_t>* find(std::string_view key) const
    {
        const auto it = std::ranges::find(keys, key, [](const auto& entry) {
            return std::string_view{entry.first};
        });
        return it == keys.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const std::pair<std::string, std::size_t>& require(std::string_view key) const
    {
        const auto* value = find(key);
        if (value == nullptr) {
            fail(line, std::format("section [{}] has no '{}'", name, key));
        }
        return *value;
    }
};

std::vector<Section> parse_sections(std::string_view text)
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
        if (const std::size_t comment = line.find_first_of("#;"); comment != std::string_view::npos) {
            line = trim(line.substr(0, comment));
        }

        if (!line.empty()) {
            if (line.front() == '[') {
                if (line.back() != ']') {
                    fail(line_number, "unterminated section header");
                }
                sections.push_back(Section{std::string{trim(line.substr(1, line.size() - 2))},
                                           line_number, {}});
            } else {
                const std::size_t equals = line.find('=');
                if (equals == std::string_view::npos) {
                    fail(line_number, std::format("expected 'key = value', got '{}'", line));
                }
                if (sections.empty()) {
                    fail(line_number, "key outside any section");
                }

                sections.back().keys.emplace_back(
                    std::string{trim(line.substr(0, equals))},
                    std::pair{std::string{trim(line.substr(equals + 1))}, line_number});
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }

    return sections;
}

const Section& require_pack_section(const std::vector<Section>& sections)
{
    const auto it = std::ranges::find(sections, "pack", &Section::name);
    if (it == sections.end()) {
        throw std::runtime_error("sprite pack has no [pack] section");
    }
    return *it;
}

} // namespace

std::string parse_atlas_filename(std::string_view text)
{
    const std::vector<Section> sections = parse_sections(text);
    return require_pack_section(sections).require("atlas").first;
}

SpritePackFile parse_sprite_pack(std::string_view text, int atlas_id, PixelSize atlas_size)
{
    const std::vector<Section> sections = parse_sections(text);
    const Section& pack_section = require_pack_section(sections);

    const auto& [atlas, atlas_line] = pack_section.require("atlas");
    const auto& [width_text, width_line] = pack_section.require("frame-width");
    const auto& [height_text, height_line] = pack_section.require("frame-height");

    const PixelSize cell{to_int(width_text, width_line, "frame-width"),
                         to_int(height_text, height_line, "frame-height")};
    if (cell.width <= 0 || cell.height <= 0) {
        fail(width_line, "frame-width and frame-height must be positive");
    }

    const int columns = atlas_size.width / cell.width;
    const int rows = atlas_size.height / cell.height;
    if (columns <= 0 || rows <= 0) {
        fail(width_line, std::format("a {}x{} atlas holds no {}x{} cells", atlas_size.width,
                                     atlas_size.height, cell.width, cell.height));
    }

    std::map<std::string, Animation, std::less<>> animations;

    for (const Section& section : sections) {
        if (section.name == "pack") {
            continue;
        }

        const auto& [frames_text, frames_line] = section.require("frames");
        const std::vector<int> indices = to_int_list(frames_text, frames_line, "frames");

        const auto& [duration_text, duration_line] = section.require("duration");
        const int duration_ms = to_int(duration_text, duration_line, "duration");
        if (duration_ms <= 0) {
            fail(duration_line, "duration must be positive");
        }

        // Bottom-centre unless stated: the anchor is the pet's feet, and for a sprite drawn
        // standing on the ground that is where they are.
        PixelOffset anchor{cell.width / 2, cell.height};
        if (const auto* value = section.find("anchor"); value != nullptr) {
            const std::vector<int> parts = to_int_list(value->first, value->second, "anchor");
            if (parts.size() != 2) {
                fail(value->second, "anchor takes two numbers");
            }
            anchor = PixelOffset{parts[0], parts[1]};
        }

        bool loop = true;
        if (const auto* value = section.find("loop"); value != nullptr) {
            loop = value->first == "true" || value->first == "1" || value->first == "yes";
        }

        std::vector<AnimationFrame> frames;
        frames.reserve(indices.size());
        for (const int index : indices) {
            if (index < 0 || index >= columns * rows) {
                fail(frames_line, std::format("frame {} is outside the {}x{} grid of cells",
                                              index, columns, rows));
            }

            frames.push_back(AnimationFrame{
                .source = PixelRect{(index % columns) * cell.width, (index / columns) * cell.height,
                                    cell.width, cell.height},
                .anchor = anchor,
                .duration = Duration{static_cast<double>(duration_ms) / 1000.0},
            });
        }

        animations.emplace(section.name, Animation{section.name, std::move(frames), loop});
    }

    if (animations.empty()) {
        throw std::runtime_error("sprite pack defines no animations");
    }

    const auto text_or = [&](std::string_view key, std::string_view fallback) {
        const auto* value = pack_section.find(key);
        return value == nullptr ? std::string{fallback} : value->first;
    };

    return SpritePackFile{
        SpritePack{text_or("id", "konqi"), text_or("name", "Konqi"),
                   text_or("artwork-licence", "CC-BY-SA-4.0"), text_or("attribution", ""),
                   atlas_id, std::move(animations)},
        atlas,
    };
}

} // namespace dp
