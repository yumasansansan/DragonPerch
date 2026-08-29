// SPDX-License-Identifier: GPL-3.0-or-later

namespace DragonPerch.Shell;

/// <summary>
/// A log file beside the executable, matching what the daemon does and for the same
/// reason: this is a windowed process with no console, so a line written to stdout goes
/// nowhere and a failure to show the menu looks exactly like a failure to start.
/// </summary>
internal static class Log
{
    private static readonly string Path = System.IO.Path.Combine(
        AppContext.BaseDirectory, "DragonPerch.Shell.log");

    private static readonly Lock Gate = new();

    public static void Line(string text)
    {
        try
        {
            lock (Gate)
            {
                File.AppendAllText(Path, $"{DateTime.Now:HH:mm:ss.fff}  {text}{Environment.NewLine}");
            }
        }
        catch (IOException)
        {
            // A log that cannot be written is not a reason to stop drawing a menu.
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    public static void Failure(string what, Exception e)
        => Line($"{what}: {e.GetType().Name}: {e.Message.ReplaceLineEndings(" ")}");
}
