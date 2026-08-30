// SPDX-License-Identifier: GPL-3.0-or-later

namespace DragonPerch.Shell;

/// <summary>
/// The running dragonperch.exe, seen from here.
/// </summary>
/// <remarks>
/// The dependency runs one way only: this program drives the daemon, and the daemon does
/// not need this program to exist. Everything here therefore degrades to "no daemon
/// running", which is a thing that happens whenever somebody quits the pets while the
/// menu is open.
/// </remarks>
internal static class Daemon
{
    /// <summary>The daemon's control window class, from src/win/control.cpp.</summary>
    private const string ControlClass = "DragonPerch.Control";

    /// <summary>
    /// Sends one command. The names are the daemon's own wire names -- text rather than an
    /// enum value, so that a shell and a daemon from different builds either understand
    /// each other or do nothing, instead of meaning whatever the third enumerator happened
    /// to be that week.
    /// </summary>
    public static bool Send(string command)
        => Native.SendCopyData(
            Native.FindWindowEx(Native.HWND_MESSAGE, IntPtr.Zero, ControlClass, null),
            command, 2000);

    /// <summary>True when a DragonPerch is running in this session.</summary>
    public static bool IsRunning() => ProcessId() != 0;

    /// <summary>
    /// The running daemon's process id, or 0 if there is none.
    /// </summary>
    /// <remarks>
    /// Found through the control window rather than by name, because a process called
    /// dragonperch.exe that is not answering on that window is not a daemon this program
    /// can talk to, and one that is answering is -- whatever it happens to be called.
    /// </remarks>
    public static int ProcessId()
    {
        IntPtr window = Native.FindWindowEx(Native.HWND_MESSAGE, IntPtr.Zero, ControlClass, null);
        if (window == IntPtr.Zero)
        {
            return 0;
        }

        _ = Native.GetWindowThreadProcessId(window, out uint pid);
        return (int)pid;
    }
}
