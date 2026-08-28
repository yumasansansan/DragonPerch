// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <charconv>
#include <concepts>
#include <string>
#include <string_view>

namespace dp {

/// Building messages without `<format>`.
///
/// `std::format` costs about a third of this binary and does so whatever is passed to it:
/// MSVC type-erases the arguments, and the visitor instantiates the floating-point path
/// even in a program that only ever formats an integer. Measured, on a program whose one
/// piece of formatting was `std::format("{} and {}", an_int, "text")`:
///
/// | | bytes |
/// |---|---:|
/// | `std::format`, integers only | 215,552 |
/// | `std::to_chars` on an integer | 10,240 |
/// | `std::to_chars` on a double | 132,608 |
///
/// So the tables are the *floating-point* conversion tables, and integer conversion is
/// free. That is the whole design here: `cat` joins strings and integers, and there is
/// deliberately no way to pass it a `double`. Nothing this program prints needs one --
/// the only fractional value anywhere is an output scale, which is a whole number.
///
/// This is not a general-purpose formatter and should not grow into one. Diagnostic code
/// is compiled out of a release build and goes on using `std::format`, where the padding
/// and alignment are worth having and the size is not paid.
namespace detail {

inline void append(std::string& out, std::string_view part)
{
    out += part;
}

inline void append(std::string& out, char part)
{
    out += part;
}

/// Null-tolerant on purpose. `glGetString` and `eglQueryString` return null when there is
/// no context or the query is not supported, and building a `string_view` from that is
/// undefined -- which is what the `std::format` these replaced did too. A message with a
/// gap in it beats a crash while reporting what went wrong.
inline void append(std::string& out, const char* part)
{
    if (part != nullptr) {
        out += part;
    }
}

/// `char` and `bool` are integral and are not numbers here: one is a character and the
/// other would print as 0 or 1, which is never what a message wants.
template <typename T>
concept Number = std::integral<T> && !std::same_as<T, char> && !std::same_as<T, bool>;

template <Number T>
void append(std::string& out, T part)
{
    // Twenty digits, a sign and a spare: more than any 64-bit value can need.
    char buffer[24];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), part);
    if (error == std::errc{}) {
        out.append(buffer, end);
    }
}

} // namespace detail

/// Joins strings and whole numbers into one message.
template <typename... Parts>
[[nodiscard]] std::string cat(const Parts&... parts)
{
    std::string out;
    (detail::append(out, parts), ...);
    return out;
}

/// Uppercase hexadecimal, for the handles and error codes that are only ever read as hex.
template <detail::Number T>
[[nodiscard]] std::string hex(T value)
{
    char buffer[24];
    const auto [end, error] =
        std::to_chars(buffer, buffer + sizeof(buffer), static_cast<unsigned long long>(value), 16);
    if (error != std::errc{}) {
        return {};
    }

    std::string out(buffer, end);
    for (char& c : out) {
        if (c >= 'a' && c <= 'f') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

} // namespace dp
