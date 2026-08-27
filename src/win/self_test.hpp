// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace dp::win::self_test {

/// Measures the two things about the overlay that cannot be reasoned about reliably.
///
/// Checking what WM_NCHITTEST returns is not a click-through test: it says what one window
/// answers when asked, not whether a real click reaches the window underneath. In the C#
/// prototype that distinction hid a bug that made the entire desktop unclickable while the
/// check reported success. So this puts a window of its own under the overlay, sends a real
/// click with SendInput, and reports whether WM_LBUTTONDOWN arrived. The click lands on
/// this program's own window, never on anything of the user's.
///
/// It also asks the shell why notifications are suppressed, because Windows turns on Do Not
/// Disturb by itself when it believes an app is running full screen.
///
/// Returns 0 on success, 1 on failure, so CI can use it directly.
int run();

} // namespace dp::win::self_test
