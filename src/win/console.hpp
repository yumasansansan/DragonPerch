// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace dp::win {

/// Borrows the launching shell's console for the diagnostic modes.
///
/// This is a GUI-subsystem binary so that nothing flashes on screen when it autostarts,
/// which means it has no console and stdout goes nowhere. `AttachConsole` gets the parent's
/// back for the modes that are meant to be run by hand.
void attach_parent_console();

} // namespace dp::win
