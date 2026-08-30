// SPDX-License-Identifier: GPL-3.0-or-later

using System.Runtime.InteropServices;
using System.Text;

namespace DragonPerch.Shell;

/// <summary>
/// The Win32 this program cannot avoid: a message-only window to be spoken to through, and
/// WM_COPYDATA to speak back to the daemon with.
/// </summary>
internal static partial class Native
{
    public const int WM_COPYDATA = 0x004A;
    public const int WM_DESTROY = 0x0002;

    public static readonly IntPtr HWND_MESSAGE = new(-3);

    [StructLayout(LayoutKind.Sequential)]
    public struct COPYDATASTRUCT
    {
        public IntPtr dwData;

        /// <summary>Unsigned, as Win32 declares it. Read as an int, a payload above 2 GB
        /// arrives as a negative length.</summary>
        public uint cbData;

        public IntPtr lpData;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct WNDCLASSEXW
    {
        public int cbSize;
        public int style;
        public IntPtr lpfnWndProc;
        public int cbClsExtra;
        public int cbWndExtra;
        public IntPtr hInstance;
        public IntPtr hIcon;
        public IntPtr hCursor;
        public IntPtr hbrBackground;
        public IntPtr lpszMenuName;
        public IntPtr lpszClassName;
        public IntPtr hIconSm;
    }

    public delegate IntPtr WndProc(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);

    /// <remarks>
    /// SetLastError, because ShellServer reads GetLastPInvokeError to tell
    /// ERROR_CLASS_ALREADY_EXISTS from a real failure. Without it the runtime never
    /// captures the error and that read returns whatever some unrelated call left behind,
    /// which makes the check look right and mean nothing.
    /// </remarks>
    [LibraryImport("user32.dll", EntryPoint = "RegisterClassExW", SetLastError = true)]
    public static partial ushort RegisterClassEx(ref WNDCLASSEXW description);

    [LibraryImport("user32.dll", EntryPoint = "CreateWindowExW", StringMarshalling = StringMarshalling.Utf16)]
    public static partial IntPtr CreateWindowEx(int exStyle, string className, string? name, int style,
                                                int x, int y, int width, int height, IntPtr parent,
                                                IntPtr menu, IntPtr instance, IntPtr param);

    [LibraryImport("user32.dll", EntryPoint = "DefWindowProcW")]
    public static partial IntPtr DefWindowProc(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);

    [LibraryImport("user32.dll", EntryPoint = "DestroyWindow")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool DestroyWindow(IntPtr hwnd);

    [LibraryImport("user32.dll", EntryPoint = "FindWindowExW", StringMarshalling = StringMarshalling.Utf16)]
    public static partial IntPtr FindWindowEx(IntPtr parent, IntPtr after, string? className, string? name);

    [LibraryImport("user32.dll", EntryPoint = "SendMessageTimeoutW")]
    public static partial IntPtr SendMessageTimeout(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam,
                                                   uint flags, uint timeout, out IntPtr result);

    [LibraryImport("user32.dll", EntryPoint = "SetForegroundWindow")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetForegroundWindow(IntPtr hwnd);

    [LibraryImport("kernel32.dll", EntryPoint = "GetModuleHandleW")]
    public static partial IntPtr GetModuleHandle(IntPtr name);

    [LibraryImport("user32.dll", EntryPoint = "GetWindowThreadProcessId")]
    public static partial uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);

    [LibraryImport("user32.dll", EntryPoint = "LoadCursorW")]
    private static partial IntPtr LoadCursor(IntPtr instance, IntPtr name);

    [LibraryImport("user32.dll", EntryPoint = "SetCursor")]
    private static partial IntPtr SetCursor(IntPtr cursor);

    /// <summary>Puts the ordinary arrow on the screen now, without waiting for a mouse move.</summary>
    /// <remarks>
    /// The menu is shown at the pointer, and the window it is shown in has no cursor of its
    /// own: enumerated at runtime, Microsoft.UI.Content.PopupWindowSiteBridge is visible with
    /// a class cursor of NULL, while the host window beside it has an arrow. A window with no
    /// class cursor does not set one, so Windows leaves whatever was already showing -- and
    /// the pointer never moves, because the menu appeared exactly where it already was, so no
    /// WM_SETCURSOR is sent to correct it. Right-clicking the tray icon leaves the shell's
    /// busy pointer on screen, and it stayed there, spinning, until the mouse was moved.
    ///
    /// Only from the second menu onwards, because the first one creates that window rather
    /// than reusing it, and creating it sets the cursor on the way.
    /// </remarks>
    public static void ShowTheArrowCursor()
    {
        // IDC_ARROW, which is an ordinal rather than a string.
        IntPtr arrow = LoadCursor(IntPtr.Zero, 32512);
        if (arrow != IntPtr.Zero)
        {
            _ = SetCursor(arrow);
        }
    }

    [LibraryImport("user32.dll", EntryPoint = "GetMonitorInfoW")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool GetMonitorInfo(IntPtr monitor, IntPtr info);

    /// <summary>
    /// The name Windows knows a monitor by: <c>\\.\DISPLAY1</c> and friends.
    /// </summary>
    /// <remarks>
    /// This is what the daemon puts in <c>OutputInfo::name</c> -- src/win/desktop_scanner.cpp
    /// reads it out of MONITORINFOEX -- and therefore what the <c>outputs</c> line of the
    /// settings file has to contain for the daemon to match it against anything.
    ///
    /// DisplayArea.DisplayId.Value is not that. It is the HMONITOR, and on the machine this
    /// was measured on the two were 65537 and \\.\DISPLAY1. Saving the number matched no
    /// monitor at all, and because an empty list is what means "every monitor", the effect
    /// of unticking one screen was that the pets disappeared from every screen.
    ///
    /// Read out of a raw buffer rather than a marshalled struct, because MONITORINFOEXW ends
    /// in a fixed thirty-two character array, and neither ByValTStr nor a fixed buffer is
    /// something LibraryImport will generate marshalling code for.
    /// </remarks>
    public static string MonitorDeviceName(ulong monitor)
    {
        // cbSize(4) + rcMonitor(16) + rcWork(16) + dwFlags(4), and then szDevice as UTF-16.
        const int deviceOffset = 40;
        const int deviceChars = 32;
        const int size = deviceOffset + (deviceChars * 2);

        IntPtr buffer = Marshal.AllocHGlobal(size);
        try
        {
            Marshal.WriteInt32(buffer, 0, size);
            if (!GetMonitorInfo((IntPtr)(long)monitor, buffer))
            {
                return string.Empty;
            }

            string device = Marshal.PtrToStringUni(buffer + deviceOffset, deviceChars)
                            ?? string.Empty;
            int end = device.IndexOf('\0');
            return end >= 0 ? device[..end] : device;
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    private const uint SMTO_ABORTIFHUNG = 0x0002;

    /// <summary>
    /// Sends one text request to a message-only window as WM_COPYDATA, and says whether the
    /// receiver acted on it.
    /// </summary>
    /// <remarks>
    /// The wire both directions use: text rather than an enum value, so that two programs
    /// from different builds either understand each other or do nothing, instead of meaning
    /// whatever the third enumerator happened to be that week.
    ///
    /// SendMessageTimeout, not PostMessage: WM_COPYDATA hands over a pointer into this
    /// process, so the receiver has to be finished with it before the buffer goes away. The
    /// timeout is what stops a wedged receiver wedging the caller with it.
    /// </remarks>
    public static bool SendCopyData(IntPtr target, string request, uint timeoutMs)
    {
        if (target == IntPtr.Zero)
        {
            return false;
        }

        byte[] bytes = Encoding.UTF8.GetBytes(request);
        IntPtr buffer = Marshal.AllocHGlobal(bytes.Length);
        try
        {
            Marshal.Copy(bytes, 0, buffer, bytes.Length);

            COPYDATASTRUCT data = new()
            {
                dwData = IntPtr.Zero,
                cbData = (uint)bytes.Length,
                lpData = buffer,
            };

            IntPtr packed = Marshal.AllocHGlobal(Marshal.SizeOf<COPYDATASTRUCT>());
            try
            {
                Marshal.StructureToPtr(data, packed, false);
                _ = SendMessageTimeout(target, WM_COPYDATA, IntPtr.Zero, packed,
                                       SMTO_ABORTIFHUNG, timeoutMs, out IntPtr result);
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

    /// <summary>The largest request worth reading.</summary>
    /// <remarks>
    /// The one message this program accepts is about thirty bytes. Anything can send this
    /// window a WM_COPYDATA, though, and without a ceiling a stranger could hand it a
    /// gigabyte and have it allocated -- twice, once as bytes and once as a string.
    /// </remarks>
    private const uint LargestRequest = 4096;

    /// <summary>
    /// Reads the text out of a WM_COPYDATA. The daemon sends UTF-8 with no terminator, the
    /// same as it sends to its own control window; `cbData` is the length.
    /// </summary>
    public static string ReadCopyData(IntPtr lparam)
    {
        COPYDATASTRUCT data = Marshal.PtrToStructure<COPYDATASTRUCT>(lparam);
        if (data.lpData == IntPtr.Zero || data.cbData == 0 || data.cbData > LargestRequest)
        {
            return string.Empty;
        }

        byte[] bytes = new byte[data.cbData];
        Marshal.Copy(data.lpData, bytes, 0, (int)data.cbData);
        return Encoding.UTF8.GetString(bytes);
    }
}
