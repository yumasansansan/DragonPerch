// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/sprite_pack_file.hpp"

#include "dragonperch/ini.hpp"

#include "dragonperch/text.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dp {
namespace {

[[noreturn]] void fail(std::size_t line, std::string_view what)
{
    throw std::runtime_error(cat("sprite pack line ", line, ": ", what));
}

int to_int(std::string_view text, std::size_t line, std::string_view what)
{
    const std::string_view value = ini::trim(text);
    int result = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec != std::errc{} || ptr != value.data() + value.size()) {
        fail(line, cat(what, " is not a number: '", value, "'"));
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
        if (!ini::trim(item).empty()) {
            values.push_back(to_int(item, line, what));
        }

        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }

    if (values.empty()) {
        fail(line, cat(what, " is empty"));
    }
    return values;
}

/// The value of a key a pack cannot do without.
const ini::Entry& require(const ini::Section& section, std::string_view key)
{
    const ini::Entry* entry = section.find(key);
    if (entry == nullptr) {
        fail(section.line, cat("section [", section.name, "] has no ", key));
    }
    return *entry;
}

const ini::Section& require_pack_section(const std::vector<ini::Section>& sections)
{
    const auto it = std::ranges::find(sections, "pack", &ini::Section::name);
    if (it == sections.end()) {
        throw std::runtime_error("sprite pack has no [pack] section");
    }
    return *it;
}

} // namespace

std::string parse_atlas_filename(std::string_view text)
{
    const std::vector<ini::Section> sections = ini::parse(text);
    return require(require_pack_section(sections), "atlas").value;
}

SpritePackFile parse_sprite_pack(std::string_view text, int atlas_id, PixelSize atlas_size)
{
    const std::vector<ini::Section> sections = ini::parse(text);
    const ini::Section& pack_section = require_pack_section(sections);

    const ini::Entry& atlas = require(pack_section, "atlas");
    const ini::Entry& width = require(pack_section, "frame-width");
    const ini::Entry& height = require(pack_section, "frame-height");

    const PixelSize cell{to_int(width.value, width.line, "frame-width"),
                         to_int(height.value, height.line, "frame-height")};
    if (cell.width <= 0 || cell.height <= 0) {
        fail(width.line, "frame-width and frame-height must be positive");
    }

    const int columns = atlas_size.width / cell.width;
    const int rows = atlas_size.height / cell.height;
    if (columns <= 0 || rows <= 0) {
        fail(width.line, cat("a ", atlas_size.width, "x", atlas_size.height, " atlas holds no ",
                             cell.width, "x", cell.height, " cells"));
    }

    std::map<std::string, Animation, std::less<>> animations;

    for (const ini::Section& section : sections) {
        if (section.name == "pack") {
            continue;
        }

        const ini::Entry& frames = require(section, "frames");
        const std::vector<int> indices = to_int_list(frames.value, frames.line, "frames");

        const ini::Entry& duration = require(section, "duration");
        const int duration_ms = to_int(duration.value, duration.line, "duration");
        if (duration_ms <= 0) {
            fail(duration.line, "duration must be positive");
        }

        // Bottom-centre unless stated: the anchor is the pet's feet, and for a sprite drawn
        // standing on the ground that is where they are.
        PixelOffset anchor{cell.width / 2, cell.height};
        if (const ini::Entry* value = section.find("anchor"); value != nullptr) {
            const std::vector<int> parts = to_int_list(value->value, value->line, "anchor");
            if (parts.size() != 2) {
                fail(value->line, "anchor takes two numbers");
            }
            anchor = PixelOffset{parts[0], parts[1]};
        }

        bool loop = true;
        if (const ini::Entry* value = section.find("loop"); value != nullptr) {
            loop = value->value == "true" || value->value == "1" || value->value == "yes";
        }

        const auto build = [&](const std::vector<int>& list, std::size_t line) {
            std::vector<AnimationFrame> frames;
            frames.reserve(list.size());
            for (const int index : list) {
                if (index < 0 || index >= columns * rows) {
                    fail(line, cat("frame ", index, " is outside the ", columns, "x", rows,
                                   " grid of cells"));
                }

                frames.push_back(AnimationFrame{
                    .source = PixelRect{(index % columns) * cell.width,
                                        (index / columns) * cell.height, cell.width, cell.height},
                    .anchor = anchor,
                    .duration = Duration{static_cast<double>(duration_ms) / 1000.0},
                });
            }
            return frames;
        };

        // Optional. Without it the renderer mirrors the right-facing frames, which is the
        // right default; with it the pack draws both directions, which is what artwork
        // containing lettering needs.
        std::vector<AnimationFrame> frames_left;
        if (const ini::Entry* value = section.find("frames-left"); value != nullptr) {
            const std::vector<int> left = to_int_list(value->value, value->line, "frames-left");
            if (left.size() != indices.size()) {
                fail(value->line, cat("frames-left has ", left.size(), " frames but frames has ",
                                        indices.size(),
                                        "; they run on the same clock and must match"));
            }
            frames_left = build(left, value->line);
        }

        animations.emplace(section.name,
                           Animation{section.name, build(indices, frames.line), loop,
                                     std::move(frames_left)});
    }

    if (animations.empty()) {
        throw std::runtime_error("sprite pack defines no animations");
    }

    const auto text_or = [&](std::string_view key, std::string_view fallback) {
        const ini::Entry* value = pack_section.find(key);
        return value == nullptr ? std::string{fallback} : value->value;
    };

    return SpritePackFile{
        SpritePack{text_or("id", "konqi"), text_or("name", "Konqi"),
                   text_or("artwork-licence", "CC-BY-SA-4.0"), text_or("attribution", ""),
                   atlas_id, std::move(animations)},
        atlas.value,
    };
}

} // namespace dp
