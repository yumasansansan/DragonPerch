// SPDX-License-Identifier: GPL-3.0-or-later

using Microsoft.UI.Xaml;

namespace DragonPerch.Shell;

/// <summary>
/// DragonPerch.Shell: the Fluent half of the Windows tray, in a process of its own.
/// </summary>
/// <remarks>
/// Why a separate program at all is §13.3 of docs/plan.md, and the short version is
/// measured rather than assumed: initialising XAML costs a process about 50 MB of private
/// bytes permanently, and closing it again gives none of that back. So the toolkit lives
/// where it can be afforded and thrown away -- here, started on demand and killable at any
/// moment -- and dragonperch.exe stays a small Win32 process that runs on its own and
/// neither knows nor cares whether this program is installed.
///
/// It does not exit when the menu closes. The second right-click should be immediate, and
/// the memory would not come back anyway.
/// </remarks>
public partial class App : Application
{
    private readonly TrayMenu _menu = new();
    private ShellServer? _server;

    public App() => InitializeComponent();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Log.Line($"launched: {Environment.CommandLine}");

        // One shell per session. The daemon starts one on hover and asks for a menu on
        // click, and those are close enough together to race; the loser exits rather than
        // leaving two windows of the same class for the daemon to choose between.
        if (ShellServer.AlreadyRunning())
        {
            Log.Line("another shell is already listening; leaving it to it");
            Exit();
            return;
        }

        _server = new ShellServer(_menu.ShowAt);
        if (!_server.Start())
        {
            // Nothing can reach this program, so it is a background process with no
            // purpose. The daemon falls back to its own Win32 menu when it cannot find us.
            Log.Line("could not create the message window");
            Exit();
            return;
        }

        // Started with a request already in hand, so the very first right-click works even
        // when nothing pre-warmed us.
        string[] argv = Environment.GetCommandLineArgs();
        if (argv.Length == 5 && argv[1] == "--menu"
            && int.TryParse(argv[2], out int x) && int.TryParse(argv[3], out int y))
        {
            Log.Line($"showing the menu at {x},{y}");
            try
            {
                _menu.ShowAt(x, y, argv[4] == "paused");
            }
            catch (Exception e)
            {
                Log.Failure("showing the menu", e);
            }
        }
        else
        {
            Log.Line($"listening; started with {argv.Length} argument(s)");
        }
    }
}
