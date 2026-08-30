// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/language.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace dp;

namespace {
/// Catch2 in this build has no way to print a std::string_view, so a CHECK on one
/// does not link. Everything the catalogue returns goes through here first.
std::string said(std::string_view text)
{
    return std::string{text};
}
} // namespace

// The Japanese below is written as escaped UTF-8 rather than pasted in, because every
// other source file in this tree is ASCII and MSVC reads a file with no byte order mark
// in the machine's code page: pasted in, on a Japanese Windows, it became mojibake and
// the string literals it produced did not parse. Real translations live in lang/*.ini,
// which is data and never goes near a compiler.
namespace {
constexpr const char* paused_ja = "\xE4\xB8\x80\xE6\x99\x82\xE5\x81\x9C\xE6\xAD\xA2";   // one time stop
constexpr const char* quit_ja = "\xE7\xB5\x82\xE4\xBA\x86";           // end
} // namespace

TEST_CASE("an empty catalogue answers with the English it was given", "[language]")
{
    // The whole point of the fallback: a build with no translations installed reads
    // exactly as it did before there was a catalogue at all.
    const Catalogue catalogue;
    CHECK(said(catalogue.get("menu.pause", "Pause")) == "Pause");
    CHECK(catalogue.empty());
}

TEST_CASE("a translated string comes back translated", "[language]")
{
    const Catalogue catalogue = Catalogue::parse(
        "[Strings]\nmenu.pause = \xE4\xB8\x80\xE6\x99\x82\xE5\x81\x9C\xE6\xAD\xA2\nmenu.quit = \xE7\xB5\x82\xE4\xBA\x86\n");

    CHECK(said(catalogue.get("menu.pause", "Pause")) == paused_ja);
    CHECK(said(catalogue.get("menu.quit", "Quit DragonPerch")) == quit_ja);

    // And one nobody has translated yet.
    CHECK(said(catalogue.get("menu.resume", "Resume")) == "Resume");
}

TEST_CASE("the bytes of a translation are carried through unchanged", "[language]")
{
    // UTF-8 in, the same UTF-8 out. Nothing on the way may decide it knows better.
    const std::string japanese = paused_ja;
    const Catalogue catalogue =
        Catalogue::parse("[Strings]\nmenu.pause = " + japanese + "\n");

    CHECK(said(catalogue.get("menu.pause", "Pause")) == japanese);
    CHECK(catalogue.get("menu.pause", "Pause").size() == 12U);
}

TEST_CASE("a line that cannot be read costs that string and not the language", "[language]")
{
    const Catalogue catalogue = Catalogue::parse(
        "[Strings]\nmenu.pause = \xE4\xB8\x80\xE6\x99\x82\xE5\x81\x9C\xE6\xAD\xA2\n"
        "this line is not a translation\nmenu.quit = \xE7\xB5\x82\xE4\xBA\x86\n");

    CHECK(catalogue.size() == 2);
    CHECK(said(catalogue.get("menu.quit", "Quit")) == quit_ja);
}

TEST_CASE("keys outside the Strings section are not translations", "[language]")
{
    const Catalogue catalogue = Catalogue::parse(
        "[Other]\nmenu.pause = wrong\n[Strings]\nmenu.pause = \xE4\xB8\x80\xE6\x99\x82\xE5\x81\x9C\xE6\xAD\xA2\n");

    CHECK(said(catalogue.get("menu.pause", "Pause")) == paused_ja);
}

TEST_CASE("what the system reports becomes tags to look for", "[language]")
{
    // POSIX and Windows describe the same thing differently, and neither is what a file
    // is called.
    CHECK(language_tags("ja_JP.UTF-8") == std::vector<std::string>{"ja-jp", "ja"});
    CHECK(language_tags("ja-JP") == std::vector<std::string>{"ja-jp", "ja"});
    CHECK(language_tags("de_DE@euro") == std::vector<std::string>{"de-de", "de"});
    CHECK(language_tags("ja") == std::vector<std::string>{"ja"});

    // "No language was chosen" is not a language.
    CHECK(language_tags("C").empty());
    CHECK(language_tags("POSIX").empty());
    CHECK(language_tags("").empty());
}

TEST_CASE("tr goes through whatever was installed", "[language]")
{
    install_catalogue(Catalogue::parse("[Strings]\nmenu.pause = \xE4\xB8\x80\xE6\x99\x82\xE5\x81\x9C\xE6\xAD\xA2\n"));
    CHECK(said(tr("menu.pause", "Pause")) == paused_ja);
    CHECK(said(tr("menu.quit", "Quit DragonPerch")) == "Quit DragonPerch");

    // Put it back, so the order these run in cannot matter to anything else.
    install_catalogue(Catalogue{});
    CHECK(said(tr("menu.pause", "Pause")) == "Pause");
}
