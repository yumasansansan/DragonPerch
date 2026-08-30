// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dp {

/// What a person reads, in the language they read.
///
/// One table for the whole project, because there are three programs in it -- the daemon,
/// the Windows shell and the KDE settings module -- and three mechanisms would mean the
/// same sentence translated three times and drifting three ways. The file is INI, which
/// both this and the C# side already have a parser for.
///
/// **Every string has an id and its English.** `tr("menu.pause", "Pause")` rather than
/// `tr("Pause")`, for two reasons. The English is the fallback, so a build with no
/// catalogue at all reads exactly as it does today and nothing has to be installed for the
/// program to work. And an id is a name we choose: it cannot contain `=`, `;` or `#`, which
/// an English sentence certainly can and which this file format would take for a separator
/// and a comment. Rewording the English also leaves the translation attached, rather than
/// silently orphaning it the way a catalogue keyed by the source text would.
class Catalogue {
public:
    /// Empty: every lookup answers with the English it was given.
    Catalogue() = default;

    /// The catalogue for the first of `languages` that has one.
    ///
    /// `start` is the directory holding the executable; `lang` is looked for there and then
    /// at `../share/dragonperch/lang`, the same rule the artwork follows and for the same
    /// reason -- an unpacked tarball has to work without being installed.
    ///
    /// Tags are tried as given and then with the region dropped, so `ja-JP` finds `ja.ini`.
    [[nodiscard]] static Catalogue load(const std::filesystem::path& start,
                                        std::span<const std::string> languages);

    /// One catalogue's text, already read. For tests, and for a caller holding it already.
    [[nodiscard]] static Catalogue parse(std::string_view text, std::string_view language = {});

    /// The local text, or `english` when there is none.
    [[nodiscard]] std::string_view get(std::string_view id, std::string_view english) const;

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] const std::string& language() const noexcept { return language_; }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    // Transparent comparison, so a lookup by string_view does not build a std::string to
    // throw away -- and this is called for every label every time a menu is opened.
    std::map<std::string, std::string, std::less<>> entries_;
    std::string language_;
};

/// The process's catalogue.
///
/// Installed once, at startup, by the head: which language somebody reads is a question
/// only the operating system can answer and the core is not allowed to ask it.
///
/// Called once and never replaced. `tr` hands out views into it, and swapping it underneath
/// a running program would leave every one of them pointing at freed memory.
void install_catalogue(Catalogue catalogue);

/// The one that call sites use. See the class comment for why there are two arguments.
[[nodiscard]] std::string_view tr(std::string_view id, std::string_view english);

/// The tags to look for, from one of the shapes an operating system reports.
///
/// Windows gives `ja-JP`; POSIX gives `ja_JP.UTF-8`, and `C` or `POSIX` when it means
/// nothing at all. Both come back here as `ja-JP` followed by `ja`, so a catalogue may be
/// as specific or as general as it likes.
[[nodiscard]] std::vector<std::string> language_tags(std::string_view reported);

} // namespace dp
