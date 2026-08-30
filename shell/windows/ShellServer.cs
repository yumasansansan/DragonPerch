// SPDX-License-Identifier: GPL-3.0-or-later

using System.Globalization;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;

namespace DragonPerch.Shell;

/// <summary>
/// The receiving end of the daemon-to-shell direction: a message-only window answering
/// WM_COPYDATA, the same mechanism and the same wire format the daemon's own control
/// window uses.
/// </summary>
/// <remarks>
/// Created on the UI thread, so its messages arrive on WinUI's own pump and the handler may
/// touch XAML directly. That is the whole reason for a window rather than a pipe: this
/// process already has a message loop, and a second thread would only have to marshal back
/// onto the first one.
///
/// Requests are text, and there is one:
/// <code>menu &lt;x&gt; &lt;y&gt; &lt;paused|running&gt;</code>
/// </remarks>
internal sealed class ShellServer
{
    /// <summary>Matched by src/win/tray.cpp, which looks this up to find a live shell.</summary>
    public const string WindowClass = "DragonPerch.Shell";

    private readonly Action<int, int, bool> _showMenu;

    // Held so the delegate is not collected while Windows still has the pointer. A
    // WndProc that has been garbage-collected is a crash at the next message, and one
    // that only happens once a menu has been open long enough for a collection to run.
    private readonly Native.WndProc _proc;

    private IntPtr _window;
    private DispatcherQueue? _ui;

    public ShellServer(Action<int, int, bool> showMenu)
    {
        _showMenu = showMenu;
        _proc = WindowProc;
    }

    /// <summary>True when another shell is already listening in this session.</summary>
    public static bool AlreadyRunning()
        => Native.FindWindowEx(Native.HWND_MESSAGE, IntPtr.Zero, WindowClass, null) != IntPtr.Zero;

    public bool Start()
    {
        _ui = DispatcherQueue.GetForCurrentThread();

        IntPtr instance = Native.GetModuleHandle(IntPtr.Zero);
        IntPtr className = Marshal.StringToHGlobalUni(WindowClass);

        Native.WNDCLASSEXW description = new()
        {
            cbSize = Marshal.SizeOf<Native.WNDCLASSEXW>(),
            lpfnWndProc = Marshal.GetFunctionPointerForDelegate(_proc),
            hInstance = instance,
            lpszClassName = className,
        };

        if (Native.RegisterClassEx(ref description) == 0
            && Marshal.GetLastPInvokeError() != 1410 /* ERROR_CLASS_ALREADY_EXISTS */)
        {
            Marshal.FreeHGlobal(className);
            return false;
        }

        _window = Native.CreateWindowEx(0, WindowClass, null, 0, 0, 0, 0, 0,
                                        Native.HWND_MESSAGE, IntPtr.Zero, instance, IntPtr.Zero);

        // Deliberately not freed: the class outlives this call and keeps the pointer. It is
        // one string for the life of the process.
        return _window != IntPtr.Zero;
    }

    /// <remarks>
    /// Everything is caught, and that is not defensiveness for its own sake. This is a
    /// reverse P/Invoke: Windows calls it, and an exception that escapes back into native
    /// code takes the process down rather than unwinding anywhere useful. Since anything
    /// in the session can send this window a message, letting one escape would mean a
    /// stranger could end the program by sending it something odd.
    /// </remarks>
    private IntPtr WindowProc(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam)
    {
        try
        {
            return Dispatch(hwnd, message, wparam, lparam);
        }
        catch (Exception e)
        {
            Log.Failure("handling a window message", e);
            return 0;
        }
    }

    private IntPtr Dispatch(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam)
    {
        if (message != Native.WM_COPYDATA)
        {
            return Native.DefWindowProc(hwnd, message, wparam, lparam);
        }

        string text = Native.ReadCopyData(lparam);
        string[] parts = text.Split(' ', StringSplitOptions.RemoveEmptyEntries);

        // InvariantCulture, because these came off a wire rather than from a person. The
        // daemon writes plain ASCII integers; parsing them against whatever culture this
        // machine is set to is asking a question nobody meant to ask, and Settings.cs is
        // careful about exactly this for exactly the same reason.
        if (parts.Length == 4 && parts[0] == "menu"
            && int.TryParse(parts[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out int x)
            && int.TryParse(parts[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out int y))
        {
            bool paused = parts[3] == "paused";

            // Answered now, shown next time round the loop. The daemon reaches this
            // through SendMessage from the thread that draws the pets, so everything done
            // before returning is time that thread spends not drawing -- and building a
            // menu is not a small thing to do inside somebody else's frame.
            //
            // Safe to defer because the text has already been copied out of the sender's
            // memory above; nothing is held past the return.
            _ = _ui?.TryEnqueue(() =>
            {
                // Caught here as well as in WindowProc, and not because two nets are better
                // than one: this body runs later, off the dispatcher, long after the window
                // procedure has returned, so the try up there does not cover it at all. An
                // exception escaping a dispatcher callback ends the process without a word.
                // That is precisely how a menu that threw while being built looked from
                // outside -- the log stopped mid-sentence at "waiting for the host to be
                // loaded", and the daemon fell back to its Win32 menu because there was no
                // longer anything to answer it.
                try
                {
                    _showMenu(x, y, paused);
                }
                catch (Exception e)
                {
                    Log.Failure("showing the menu", e);
                }
            });
            return 1;
        }

        // An unknown request is ignored rather than guessed at, for the same reason the
        // daemon ignores unknown commands: the two ends may be different builds.
        return 0;
    }
}
