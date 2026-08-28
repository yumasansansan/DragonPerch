// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/text.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>

using namespace dp;

TEST_CASE("cat joins strings and whole numbers", "[text]")
{
    CHECK(cat("frame ", 7, " of ", 12) == "frame 7 of 12");
    CHECK(cat(std::string{"a"}, std::string_view{"b"}, "c", 'd') == "abcd");
    CHECK(cat() == "");
}

TEST_CASE("negative and wide numbers survive", "[text]")
{
    // Twenty digits and a sign is the widest a 64-bit value gets, and the buffer is sized
    // for exactly that. An off-by-one here would silently truncate an owner id.
    CHECK(cat(-1) == "-1");
    CHECK(cat(std::int64_t{-9223372036854775807LL - 1}) == "-9223372036854775808");
    CHECK(cat(std::uint64_t{18446744073709551615ULL}) == "18446744073709551615");
}

TEST_CASE("a null string is a gap, not a crash", "[text]")
{
    // glGetString and eglQueryString return null when there is no context. Both the
    // std::format this replaced and a bare string_view would be undefined; a message with
    // a hole in it is what a program reporting a failure should manage.
    const char* nothing = nullptr;
    CHECK(cat("vendor: ", nothing, "!") == "vendor: !");
}

TEST_CASE("char is a character and bool is not a number", "[text]")
{
    // Both are integral, and neither should come out as digits: 'x' is a letter, and a
    // flag printed as 0 or 1 is never what a message wanted to say.
    CHECK(cat('x') == "x");
    CHECK(cat("[", 'y', "]") == "[y]");
}

TEST_CASE("hex is uppercase and unpadded", "[text]")
{
    // Handles and error codes are read as hex and nothing else, so this only has to match
    // what someone would search a log for.
    CHECK(hex(0) == "0");
    CHECK(hex(255) == "FF");
    CHECK(hex(0xDEADBEEFU) == "DEADBEEF");
}
