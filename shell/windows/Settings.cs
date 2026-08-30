// SPDX-License-Identifier: GPL-3.0-or-later

using System.Globalization;
using System.Text;

namespace DragonPerch.Shell;

/// <summary>
/// What a person is allowed to change, and nothing else.
/// </summary>
/// <remarks>
/// The same struct as <c>dp::Settings</c> in src/core/dragonperch/settings.hpp, and the
/// same INI file. Two implementations of one format, which is deliberate rather than an
/// oversight: the Linux settings program is a KDE Config Module and will read and write
/// this file with KConfig, so the format was always going to have more than one writer.
/// The file is the contract; the code is not shared and cannot be.
///
/// That makes it worth being exact about the things the C++ side is exact about. Nothing
/// here throws, unreadable values keep their default, and the ranges below are the same
/// ranges -- a settings program that saves a value the daemon then clamps is a settings
/// program that lies about what it did.
/// </remarks>
internal sealed class Settings
{
    public const string Section = "DragonPerch";

    /// <summary>How many of each mascot. Three mascots at 2 is six dragons.</summary>
    public int PetsPerMascot { get; set; } = 1;

    /// <summary>Pack ids. Empty means every one that is installed.</summary>
    public List<string> Mascots { get; set; } = [];

    /// <summary>Pixels per second. The walk cycle was drawn against 42.</summary>
    public double WalkSpeed { get; set; } = 42.0;

    /// <summary>Mean seconds between spontaneous pauses. 0 stops them happening.</summary>
    public double IdleInterval { get; set; } = 9.0;

    /// <summary>Monitor names. Empty means all of them.</summary>
    public List<string> Outputs { get; set; } = [];

    /// <summary>Hide the pets on a monitor showing something full screen.</summary>
    public bool PauseForFullscreen { get; set; } = true;

    /// <summary>%APPDATA%\DragonPerch\dragonperchrc.</summary>
    public static string Path => System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "DragonPerch", "dragonperchrc");

    public static Settings Load()
    {
        try
        {
            return Parse(File.ReadAllText(Path));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            // Nothing saved yet, which is the ordinary case the first time.
            return new Settings();
        }
    }

    public bool Save()
    {
        try
        {
            string? directory = System.IO.Path.GetDirectoryName(Path);
            if (directory is not null)
            {
                Directory.CreateDirectory(directory);
            }

            File.WriteAllText(Path, Write(), new UTF8Encoding(false));
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Failure("saving the settings", e);
            return false;
        }
    }

    /// <summary>
    /// Reads settings from INI. Unknown keys and unreadable values are ignored rather than
    /// refused, exactly as the daemon does: this file is edited by hand and written by two
    /// other programs, and losing every setting because one line is wrong is worse than
    /// losing the one line.
    /// </summary>
    public static Settings Parse(string text)
    {
        Settings settings = new();
        bool inSection = false;

        foreach (string raw in text.Split('\n'))
        {
            string line = Strip(raw);
            if (line.Length == 0)
            {
                continue;
            }

            if (line[0] == '[')
            {
                int close = line.IndexOf(']');
                inSection = close > 0 && line[1..close].Trim() == Section;
                continue;
            }

            if (!inSection)
            {
                continue;
            }

            int equals = line.IndexOf('=');
            if (equals < 0)
            {
                continue;
            }

            string key = line[..equals].Trim();
            string value = line[(equals + 1)..].Trim();

            switch (key)
            {
                case "pets-per-mascot":
                    if (int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int n))
                    {
                        settings.PetsPerMascot = Math.Clamp(n, 0, 64);
                    }
                    break;

                case "mascots":
                    settings.Mascots = SplitList(value);
                    break;

                case "walk-speed":
                    if (TryDouble(value, out double speed))
                    {
                        settings.WalkSpeed = Math.Clamp(speed, 1.0, 1000.0);
                    }
                    break;

                case "idle-interval":
                    if (TryDouble(value, out double idle))
                    {
                        settings.IdleInterval = Math.Clamp(idle, 0.0, 3600.0);
                    }
                    break;

                case "outputs":
                    settings.Outputs = SplitList(value);
                    break;

                case "pause-for-fullscreen":
                    // What KConfig writes is true/false; what people type is anybody's guess.
                    if (value is "true" or "1" or "yes" or "on")
                    {
                        settings.PauseForFullscreen = true;
                    }
                    else if (value is "false" or "0" or "no" or "off")
                    {
                        settings.PauseForFullscreen = false;
                    }
                    break;
            }
        }

        return settings;
    }

    /// <summary>
    /// Writes settings as INI, with a comment saying what each one does.
    /// </summary>
    /// <remarks>
    /// The whole file every time, including the defaults, and worded the same as the
    /// daemon's own writer: a file that has been through this program and a file that has
    /// been through the daemon should not look different to somebody comparing them.
    /// </remarks>
    public string Write()
    {
        StringBuilder text = new();
        text.Append("; DragonPerch settings. Written by the settings program and safe to edit by hand;\n");
        text.Append("; a line that cannot be read is ignored and its setting keeps the default.\n");
        text.Append($"\n[{Section}]\n");
        text.Append("\n; How many of each mascot. Three mascots at 2 is six dragons.\n");
        text.Append($"pets-per-mascot = {PetsPerMascot.ToString(CultureInfo.InvariantCulture)}\n");
        text.Append("\n; Which mascots, by pack id: konqi, katie, kori. Empty means all of them.\n");
        text.Append($"mascots = {string.Join(", ", Mascots)}\n");
        text.Append("\n; Pixels per second. The walk cycle was drawn against 42; much faster and the\n");
        text.Append("; feet slide, much slower and it moonwalks.\n");
        text.Append($"walk-speed = {TwoPlaces(WalkSpeed)}\n");
        text.Append("\n; Mean seconds between spontaneous pauses. 0 stops them happening.\n");
        text.Append($"idle-interval = {TwoPlaces(IdleInterval)}\n");
        text.Append("\n; Which monitors, by the name the system reports. Empty means all of them.\n");
        text.Append($"outputs = {string.Join(", ", Outputs)}\n");
        text.Append("\n; Hide the pets on a monitor showing something full screen.\n");
        text.Append($"pause-for-fullscreen = {(PauseForFullscreen ? "true" : "false")}\n");
        return text.ToString();
    }

    /// <summary>Comments and whitespace off. Both markers, because '#' is what people type
    /// and ';' is what KConfig writes.</summary>
    private static string Strip(string line)
    {
        int cut = line.IndexOfAny([';', '#']);
        return (cut >= 0 ? line[..cut] : line).Trim();
    }

    private static List<string> SplitList(string value)
        => [.. value.Split(',').Select(part => part.Trim()).Where(part => part.Length > 0)];

    /// <summary>
    /// InvariantCulture, because the daemon reads this file with the C locale and a decimal
    /// comma would come back as a different number or as none at all. IsFinite, because
    /// both parsers accept "NaN" and "Infinity" and neither clamp does anything useful with
    /// one -- see the same guard in dragonperch/settings.cpp.
    /// </summary>
    private static bool TryDouble(string value, out double result)
        => double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out result)
           && double.IsFinite(result);

    private static string TwoPlaces(double value)
        => value.ToString("0.00", CultureInfo.InvariantCulture);
}
