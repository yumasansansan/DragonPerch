// SPDX-License-Identifier: GPL-3.0-or-later

using System.Runtime.InteropServices;
using System.Text;

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

    private const uint SMTO_ABORTIFHUNG = 0x0002;

    /// <summary>
    /// Sends one command. The names are the daemon's own wire names -- text rather than an
    /// enum value, so that a shell and a daemon from different builds either understand
    /// each other or do nothing, instead of meaning whatever the third enumerator happened
    /// to be that week.
    /// </summary>
    public static bool Send(string command)
    {
        IntPtr target = Native.FindWindowEx(Native.HWND_MESSAGE, IntPtr.Zero, ControlClass, null);
        if (target == IntPtr.Zero)
        {
            return false;
        }

        byte[] bytes = Encoding.UTF8.GetBytes(command);
        IntPtr buffer = Marshal.AllocHGlobal(bytes.Length);
        try
        {
            Marshal.Copy(bytes, 0, buffer, bytes.Length);

            Native.COPYDATASTRUCT data = new()
            {
                dwData = IntPtr.Zero,
                cbData = bytes.Length,
                lpData = buffer,
            };

            IntPtr packed = Marshal.AllocHGlobal(Marshal.SizeOf<Native.COPYDATASTRUCT>());
            try
            {
                Marshal.StructureToPtr(data, packed, false);

                // SendMessageTimeout, not PostMessage: WM_COPYDATA hands over a pointer
                // into this process, so the receiver has to have finished with it before
                // the buffer goes away. The timeout is what stops a wedged daemon wedging
                // the menu with it.
                _ = Native.SendMessageTimeout(target, Native.WM_COPYDATA, IntPtr.Zero, packed,
                                              SMTO_ABORTIFHUNG, 2000, out IntPtr result);
                return result != IntPtr.Zero;
            }
            finally
            {
                Marshal.FreeHGlobal(packed);
            }
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

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
