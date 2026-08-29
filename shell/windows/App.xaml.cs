// SPDX-License-Identifier: GPL-3.0-or-later

using System.Diagnostics;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DragonPerch.Shell;

/// <summary>
/// DragonPerch.Shell: the Fluent half of the Windows tray, in a process of its own.
/// </summary>
/// <remarks>
/// Why a separate program at all is §13.3 of docs/plan.md, and the short version is
/// measured rather than assumed: initialising XAML costs a process about 40 MB of private
/// bytes permanently, and closing it again gives none of that back. So the toolkit lives
/// where it can be afforded and thrown away -- here, started on demand and killable at any
/// moment -- and dragonperch.exe stays a small Win32 process that runs on its own and
/// neither knows nor cares whether this program is installed.
///
/// It does not exit when the menu closes; the second right-click should be immediate, and
/// the memory would not come back anyway. It does exit when the daemon does, which is the
/// only thing it exists to serve.
/// </remarks>
public partial class App : Application
{
    private readonly TrayMenu _menu = new();
    private ShellServer? _server;
    private DispatcherQueue? _ui;
    private Process? _daemon;

    public App() => InitializeComponent();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Log.Line($"launched: {Environment.CommandLine}");
        _ui = DispatcherQueue.GetForCurrentThread();

        // One shell per session. The daemon starts one on hover and asks for a menu on
        // click, and those are close enough together to race; the loser exits rather than
        // leaving two windows of the same class for the daemon to choose between.
        if (ShellServer.AlreadyRunning())
        {
            Log.Line("another shell is already listening; leaving it to it");
            Exit();
            return;
        }

        if (!WatchTheDaemon())
        {
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

        // A menu asked for on the command line. The daemon never does this -- it pre-warms
        // silently and then asks over WM_COPYDATA -- but it is the only way to put this
        // program's menu on the screen by hand, which is worth keeping for anyone checking
        // whether the shell works at all.
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
            Log.Line("listening");
        }
    }

    /// <summary>
    /// Ties this process's life to the daemon's. False if it has already ended.
    /// </summary>
    /// <remarks>
    /// Without this the shell is immortal: it is started by the daemon, is never a child of
    /// it in any sense Windows enforces, and holds about 40 MB for a menu that can no
    /// longer do anything. Quitting the pets left it behind in the process list.
    ///
    /// Waiting on the process handle rather than on a goodbye message from the daemon,
    /// because the daemon can be killed, can crash, or can be closed with its console --
    /// and in none of those cases does it get to say anything.
    /// </remarks>
    private bool WatchTheDaemon()
    {
        int pid = Daemon.ProcessId();
        if (pid == 0)
        {
            Log.Line("no daemon is running; nothing for this program to do");
            Exit();
            return false;
        }

        try
        {
            _daemon = Process.GetProcessById(pid);
            _daemon.EnableRaisingEvents = true;
            _daemon.Exited += (_, _) =>
            {
                // On a thread-pool thread, so it has to come back to the UI thread before
                // touching anything XAML owns.
                Log.Line("the daemon has gone; exiting");
                _ui?.TryEnqueue(Exit);
            };
        }
        catch (ArgumentException)
        {
            // It ended between finding its window and opening its process.
            Log.Line("the daemon went away while we were starting");
            Exit();
            return false;
        }

        Log.Line($"watching daemon {pid}");
        return true;
    }
}
