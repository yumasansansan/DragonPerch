// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string.h>

namespace dp::wl {
namespace detail {

// strerror_r has two incompatible signatures and which one is declared depends on feature
// test macros: glibc's returns char* and is free to ignore the buffer entirely, handing back
// a pointer to a constant string instead, while POSIX's returns int and always writes into
// the buffer. Overloading on the return type picks the right handling without testing a
// macro, which would be guessing at the same thing less reliably.

[[nodiscard]] inline const char* strerror_result(char* result, const char* buffer) noexcept
{
    return result != nullptr ? result : buffer;
}

[[nodiscard]] inline const char* strerror_result(int result, const char* buffer) noexcept
{
    // Truncation (ERANGE) still leaves a usable message behind; it is only a bad errno value
    // that writes nothing at all, and the buffer starts zeroed so an empty one says which.
    return result == 0 || buffer[0] != '\0' ? buffer : "unknown error";
}

} // namespace detail

/// The message for an errno value, thread safely.
///
/// std::strerror returns a pointer into a single static buffer shared by the whole process,
/// so two threads formatting an error at the same time can each end up reading the other's
/// text -- or reading one halfway through being written. This daemon has two threads that do
/// exactly that: SessionBus::run() reports bus failures from its own worker while the main
/// thread is reporting its own the same way. strerror_r writes into a caller-supplied buffer
/// instead, which is what makes this safe rather than merely quieter.
[[nodiscard]] inline std::string errno_text(int errnum)
{
    char buffer[256] = {};
    return detail::strerror_result(strerror_r(errnum, buffer, sizeof buffer), buffer);
}

} // namespace dp::wl
