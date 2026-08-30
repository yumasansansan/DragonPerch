// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragonperch/language.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

/// The translation catalogue: a file this program reads and did not write.
///
/// It is the newest boundary in the project and the least controlled. A `.deb` installs
/// one, but so does anybody who drops a file into `lang/` -- that is the point of the
/// design, and it means arbitrary bytes reach this parser. It shares the INI parser with
/// the settings, so what is new here is what happens *after* parsing.
///
/// Nothing is caught. Catalogue::parse promises not to throw -- a broken translation must
/// cost that string and not the language -- so an exception escaping is a real finding.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    const dp::Catalogue catalogue = dp::Catalogue::parse(text, "xx");

    // The fallback is the whole contract, and this is how to state it without assuming
    // anything about what is in the file. Asked for the same id with two different English
    // strings, a catalogue must either answer with each of them -- it has no translation
    // and handed back what it was given -- or answer with the same thing twice, which is
    // the translation it does have. Anything else means the fallback leaked.
    //
    // Written this way after the obvious version was wrong: it asserted that one particular
    // id was absent, and libFuzzer reads the string literals in the binary and puts them
    // into the input. It found `no.such.id.anywhere = ...` in under a minute, and the
    // catalogue had done exactly the right thing with it.
    const std::string_view first = "Quit DragonPerch";
    const std::string_view second = "Pause";

    for (const char* id : {"menu.pause", "settings.pets", "no.such.id.anywhere", "", "="}) {
        const std::string_view a = catalogue.get(id, first);
        const std::string_view b = catalogue.get(id, second);

        const bool absent = a == first && b == second;
        const bool present = a == b;
        if (!absent && !present) {
            std::abort();
        }
    }

    // An empty catalogue is not a special case: it answers with the English every time.
    if (catalogue.empty() && catalogue.get("menu.pause", first) != first) {
        std::abort();
    }

    // Installing it must not disturb any of that. tr goes through the same table, and the
    // views it hands out point into the catalogue this owns -- which is what makes
    // installing one and then reading from it worth checking rather than assuming.
    dp::install_catalogue(catalogue);
    if (dp::tr("menu.pause", first) != catalogue.get("menu.pause", first)) {
        std::abort();
    }
    dp::install_catalogue(dp::Catalogue{});

    return 0;
}
