// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace dp::win {

/// A way to ask a running DragonPerch to quit that does not go through the console.
///
/// Ctrl+C very nearly works and cannot be relied on. This is a GUI-subsystem binary -- so
/// that nothing flashes on screen when it autostarts -- which means the shell does not wait
/// for it and the prompt comes straight back. Whether a Ctrl+C at that prompt ever becomes
/// a CTRL_C_EVENT is then up to the shell: `cmd` sends one, and PowerShell 7 does not,
/// because PSReadLine handles the key itself to clear the input line.
///
/// So there is a named event as well. `dragonperch --stop` sets it, the render loop sees
/// it, and the process unwinds properly -- which is what gets the DirectComposition device
/// and the overlay windows torn down rather than abandoned.
///
/// Local\ rather than Global\: one running instance per session is the thing being stopped,
/// and a name in the global namespace would let one user's --stop reach another's pets.

/// Creates the event, if it does not exist. Call once at startup.
void create_stop_signal();

/// True once somebody has asked us to stop. Cheap enough to call every frame.
[[nodiscard]] bool stop_requested();

/// Sets the event on whichever instance is running. False if there is nothing to stop.
[[nodiscard]] bool raise_stop_signal();

} // namespace dp::win
