// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/language.hpp"

#include "dragonperch/ini.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ios>
#include <system_error>
#include <utility>

namespace dp {
namespace {

constexpr std::string_view section_name = "Strings";

/// The one catalogue this process reads from.
///
/// A file-scope object, which is what a translation table is: there is one language and
/// every call site wants it. The alternative is threading it through the constructor of
/// everything that has a label on it, which is most of the program.
Catalogue& current()
{
    static Catalogue catalogue;
    return catalogue;
}

std::string lowered(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return {};
    }

    const std::streamoff size = stream.tellg();
    if (size <= 0) {
        return {};
    }

    std::string text(static_cast<std::size_t>(size), 0);
    stream.seekg(0);
    stream.read(text.data(), size);
    text.resize(static_cast<std::size_t>(stream.gcount()));
    return text;
}

} // namespace

std::vector<std::string> language_tags(std::string_view reported)
{
    // Everything after a `.` or an `@` is an encoding or a modifier -- `ja_JP.UTF-8`,
    // `de_DE@euro` -- and says nothing about which words to use.
    const std::size_t cut = reported.find_first_of(".@");
    std::string tag{cut == std::string_view::npos ? reported : reported.substr(0, cut)};

    // POSIX separates language and region with `_`, BCP 47 and Windows with `-`. One shape
    // from here on.
    std::ranges::replace(tag, '_', '-');
    tag = lowered(tag);

    // `C` and `POSIX` mean "no language was chosen", not a language called C.
    if (tag.empty() || tag == "c" || tag == "posix") {
        return {};
    }

    std::vector<std::string> tags;
    tags.push_back(tag);

    if (const std::size_t dash = tag.find('-'); dash != std::string::npos) {
        tags.push_back(tag.substr(0, dash));
    }
    return tags;
}

Catalogue Catalogue::parse(std::string_view text, std::string_view language)
{
    Catalogue catalogue;
    catalogue.language_ = language;

    std::vector<ini::Section> sections;
    try {
        // Line by line, as the settings are read: one bad line in a translation should cost
        // that string and not the language.
        sections = ini::parse(text, ini::OnBadLine::skip);
    } catch (const std::exception&) {
        return catalogue;
    }

    for (const ini::Section& section : sections) {
        if (section.name != section_name) {
            continue;
        }
        for (const ini::Entry& entry : section.entries) {
            if (!entry.key.empty() && !entry.value.empty()) {
                catalogue.entries_.insert_or_assign(entry.key, entry.value);
            }
        }
    }

    return catalogue;
}

Catalogue Catalogue::load(const std::filesystem::path& start,
                          std::span<const std::string> languages)
{
    for (const char* relative : {"lang", "../share/dragonperch/lang"}) {
        const std::filesystem::path directory = start / relative;

        std::error_code ignored;
        if (!std::filesystem::exists(directory, ignored)) {
            continue;
        }

        for (const std::string& tag : languages) {
            const std::string text = read_file(directory / (tag + ".ini"));
            if (!text.empty()) {
                Catalogue catalogue = parse(text, tag);
                if (!catalogue.empty()) {
                    return catalogue;
                }
            }
        }
    }

    return {};
}

std::string_view Catalogue::get(std::string_view id, std::string_view english) const
{
    const auto it = entries_.find(id);
    return it == entries_.end() ? english : std::string_view{it->second};
}

void install_catalogue(Catalogue catalogue)
{
    current() = std::move(catalogue);
}

std::string_view tr(std::string_view id, std::string_view english)
{
    return current().get(id, english);
}

} // namespace dp
