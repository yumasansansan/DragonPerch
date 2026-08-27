// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace dp::wl {

/// Writes a line to stderr.
///
/// Unlike the Windows head there is no log file here, and deliberately: this is a console
/// binary launched from a terminal or from a desktop file, and in the second case the
/// session journal already collects stderr. A file beside the executable would be a second
/// place to look with nothing in it that `journalctl --user` does not have.
void log_line(std::string_view text);

} // namespace dp::wl
