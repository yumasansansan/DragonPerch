// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace dp::win {

/// Borrows the launching shell's console for the diagnostic modes.
///
/// This is a GUI-subsystem binary so that nothing flashes on screen when it autostarts,
/// which means it has no console and stdout goes nowhere. `AttachConsole` gets the parent's
/// back for the modes that are meant to be run by hand.
void attach_parent_console();

/// Asks to be told about Ctrl+C, the console closing, and the user logging out.
///
/// `on_stop` runs on a thread of the OS's choosing with about five seconds before the
/// process is killed anyway, so it must do nothing but set a flag.
void handle_console_stop(void (*on_stop)());

} // namespace dp::win
