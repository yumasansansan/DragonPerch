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
        public int cbData;
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

    /// <summary>
    /// Reads the text out of a WM_COPYDATA. The daemon sends UTF-8 with no terminator, the
    /// same as it sends to its own control window; `cbData` is the length.
    /// </summary>
    public static string ReadCopyData(IntPtr lparam)
    {
        COPYDATASTRUCT data = Marshal.PtrToStructure<COPYDATASTRUCT>(lparam);
        if (data.lpData == IntPtr.Zero || data.cbData <= 0)
        {
            return string.Empty;
        }

        byte[] bytes = new byte[data.cbData];
        Marshal.Copy(data.lpData, bytes, 0, data.cbData);
        return Encoding.UTF8.GetString(bytes);
    }
}
