// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace dp::win {

/// UTF-16 to UTF-8. Windows APIs speak the former, the log and every message speak the
/// latter; a `std::string(w.begin(), w.end())` shortcut silently truncates anything outside
/// Latin-1, which on this machine includes most window titles.
std::string to_utf8(std::wstring_view text);

/// And back again, for the Win32 calls that take wide strings. Translated text arrives as
/// UTF-8 -- the catalogue is a file, and a file has bytes -- and menus want UTF-16.
std::wstring to_utf16(std::string_view text);

/// Writes a line to stdout *and* to a log file beside the executable.
///
/// A GUI-subsystem binary has no console of its own, and whether `AttachConsole` reaches
/// the caller's stream depends on how it was launched and whether output was redirected.
/// That is a bad thing to depend on while diagnosing why nothing appeared on screen: the
/// first run of the composition probe drew nothing *and* printed nothing, which left no way
/// to tell a failed device from a failed window. The file is the reliable half.
void log_line(std::string_view text);

/// Path of the log file, so the caller can tell the user where to look.
const char* log_path();

} // namespace dp::win
