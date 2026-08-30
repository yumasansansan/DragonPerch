// SPDX-License-Identifier: GPL-3.0-or-later

using System.Globalization;

namespace DragonPerch.Shell;

/// <summary>
/// What a person reads, in the language they read.
/// </summary>
/// <remarks>
/// The same catalogue the daemon reads, in the same format, found in the same place. See
/// src/core/dragonperch/language.hpp for why the key is an id rather than the English and
/// why every call site carries its English anyway.
///
/// A second implementation of a file format, like <see cref="Settings"/>, and for the same
/// unavoidable reason: this program is C# and cannot call the core. It is thirty lines of
/// `key = value`, which is the part of that arrangement least likely to go wrong -- and the
/// worst it can do is fall back to English, rather than lose somebody's settings.
/// </remarks>
internal static class Strings
{
    private static readonly Dictionary<string, string> Entries = Load();

    /// <summary>The local text, or <paramref name="english"/> when there is none.</summary>
    public static string Get(string id, string english)
        => Entries.TryGetValue(id, out string? text) ? text : english;

    /// <summary>Which catalogue was loaded, for the log. Empty when none was.</summary>
    public static string Language { get; private set; } = string.Empty;

    /// <summary>
    /// The tags to look for, most specific first: `ja-JP` then `ja`.
    /// </summary>
    private static List<string> Tags()
    {
        List<string> tags = [];
        for (CultureInfo culture = CultureInfo.CurrentUICulture;
             !string.IsNullOrEmpty(culture.Name);
             culture = culture.Parent)
        {
            string tag = culture.Name.ToLowerInvariant();
            if (!tags.Contains(tag))
            {
                tags.Add(tag);
            }

            // InvariantCulture's parent is itself, so this would not otherwise stop.
            if (ReferenceEquals(culture, culture.Parent))
            {
                break;
            }
        }
        return tags;
    }

    private static Dictionary<string, string> Load()
    {
        foreach (string directory in new[]
                 {
                     Path.Combine(AppContext.BaseDirectory, "lang"),
                     Path.Combine(AppContext.BaseDirectory, "..", "share", "dragonperch", "lang"),
                 })
        {
            foreach (string tag in Tags())
            {
                try
                {
                    string path = Path.Combine(directory, tag + ".ini");
                    if (!File.Exists(path))
                    {
                        continue;
                    }

                    Dictionary<string, string> entries = Parse(File.ReadAllText(path));
                    if (entries.Count > 0)
                    {
                        Language = tag;
                        Log.Line($"language: {tag} ({entries.Count} strings)");
                        return entries;
                    }
                }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException)
                {
                    // Try the next one. A catalogue that cannot be read is a catalogue that
                    // is not there, and English is what happens then.
                }
            }
        }

        return [];
    }

    /// <summary>
    /// The `[Strings]` section of one catalogue. Unreadable lines are skipped, exactly as
    /// the daemon skips them: a bad line costs that string and not the language.
    /// </summary>
    private static Dictionary<string, string> Parse(string text)
    {
        Dictionary<string, string> entries = [];
        bool inSection = false;

        foreach (string raw in text.Split('\n'))
        {
            int cut = raw.IndexOfAny([';', '#']);
            string line = (cut >= 0 ? raw[..cut] : raw).Trim();
            if (line.Length == 0)
            {
                continue;
            }

            if (line[0] == '[')
            {
                inSection = line[^1] == ']' && line[1..^1].Trim() == "Strings";
                continue;
            }

            int equals = line.IndexOf('=');
            if (!inSection || equals < 0)
            {
                continue;
            }

            string id = line[..equals].Trim();
            string value = line[(equals + 1)..].Trim();
            if (id.Length > 0 && value.Length > 0)
            {
                entries[id] = value;
            }
        }

        return entries;
    }
}
