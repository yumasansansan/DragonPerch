// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragonperch/ini.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string_view>
#include <vector>

/// The INI parser, which every text file this program reads goes through.
///
/// It is allowed to throw -- a malformed file is a refusal, not a guess -- so
/// std::exception is caught and ignored. What is not allowed is anything else: reading past
/// the end of the buffer, an unbounded allocation, a hang. Those are what the sanitizers
/// under this are watching for, and what a fixed set of test cases will never look for.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    try {
        const std::vector<dp::ini::Section> sections = dp::ini::parse(text);

        // Walked rather than counted, because find() and the entry spans are as much of the
        // interface as parse() is, and a section that reports entries it does not have
        // would go unnoticed by a fuzzer that only ever called parse.
        for (const dp::ini::Section& section : sections) {
            for (const dp::ini::Entry& entry : section.entries) {
                (void)section.find(entry.key);
            }
        }
    // Swallowing it is the point: a refusal is the documented behaviour, and what this
    // target watches for is everything else.
    // NOLINTNEXTLINE(bugprone-empty-catch)
    } catch (const std::exception&) {
        // A refusal, which is the documented behaviour.
    }

    return 0;
}
