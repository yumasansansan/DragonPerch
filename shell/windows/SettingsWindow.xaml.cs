// SPDX-License-Identifier: GPL-3.0-or-later

using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Graphics;

namespace DragonPerch.Shell;

/// <summary>
/// The settings window: milestone 10's Windows half.
/// </summary>
/// <remarks>
/// It writes the same INI file the daemon reads and then asks the daemon to re-read it, so
/// a change is visible before the window has finished closing. Nothing is applied without
/// Apply being pressed -- a settings window that acts on every keystroke would have the
/// pets respawning while somebody is still typing a number.
/// </remarks>
internal sealed partial class SettingsWindow : Window
{
    private Settings _settings = new();
    private readonly List<(string Id, CheckBox Box)> _mascots = [];
    private readonly List<(string Name, CheckBox Box)> _outputs = [];

    public SettingsWindow()
    {
        InitializeComponent();

        Title = "DragonPerch";
        AppWindow.Resize(new SizeInt32(760, 720));

        // Set Acrylic Backdrop
        SystemBackdrop = new Microsoft.UI.Xaml.Media.DesktopAcrylicBackdrop();

        // Extend content into title bar
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);

        SpeedSlider.ValueChanged += (_, e) => SpeedValue.Text = ((int)e.NewValue).ToString();
        FullscreenToggle.Toggled += (_, _) => ShowToggleState();

        Load();

        // After the tree is up, or there is nothing to take it from. Programmatic, so no
        // focus rectangle is drawn: the point is that the window opens with nothing looking
        // like it has been clicked on.
        Root.Loaded += (_, _) => Root.Focus(FocusState.Programmatic);
    }

    /// <summary>The pack ids that are actually installed, from the assets beside us.</summary>
    /// <remarks>
    /// Read off the disk rather than hard-coded, for the same reason the daemon does it:
    /// somebody who drops a fourth mascot into assets/ should be able to turn it on here
    /// without this program having been rebuilt.
    /// </remarks>
    private static List<string> InstalledMascots()
    {
        foreach (string candidate in new[]
                 {
                     Path.Combine(AppContext.BaseDirectory, "assets"),
                     Path.Combine(AppContext.BaseDirectory, "..", "share", "dragonperch", "assets"),
                 })
        {
            try
            {
                if (Directory.Exists(candidate))
                {
                    List<string> found = [.. Directory.GetDirectories(candidate)
                        .Select(Path.GetFileName)
                        .Where(name => name is not null)
                        .Select(name => name!)
                        .Where(name => File.Exists(Path.Combine(candidate, name, name + ".ini")))
                        .Order()];

                    if (found.Count > 0)
                    {
                        return found;
                    }
                }
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                // Try the next place.
            }
        }

        return [];
    }

    private void Load()
    {
        _settings = Settings.Load();

        PetsBox.Value = _settings.PetsPerMascot;
        IdleBox.Value = _settings.IdleInterval;
        SpeedSlider.Value = Math.Clamp(_settings.WalkSpeed, SpeedSlider.Minimum, SpeedSlider.Maximum);
        SpeedValue.Text = ((int)SpeedSlider.Value).ToString();
        FullscreenToggle.IsOn = _settings.PauseForFullscreen;

        // Explicitly, not by way of the Toggled event above: assigning IsOn only raises it
        // when the value changes, so a saved setting that matches the default -- which is
        // the common case -- would have left the label blank.
        ShowToggleState();

        // An empty list in the file means "all of them", so every box starts ticked. That
        // is also why turning them all off saves an empty list rather than nothing: the two
        // mean the same thing to the daemon, and the wording on the card says so.
        foreach (string id in InstalledMascots())
        {
            CheckBox box = new()
            {
                Content = Pretty(id),
                IsChecked = _settings.Mascots.Count == 0 || _settings.Mascots.Contains(id),

                // Centred against the box, and the top padding taken off so that centring
                // means what it says. WinUI's CheckBoxPadding is 8,5,0,0: five pixels of it
                // are above the label, put there to sit the text against the top of the box
                // the way Segoe UI's ascent wants. Centre the content and those five pixels
                // are still there, pushing it down by half of them -- measured at exactly
                // two pixels low, against a twenty pixel box.
                //
                // Set here rather than as an implicit style, which on a templated control
                // replaces the default style rather than adding to it.
                VerticalContentAlignment = VerticalAlignment.Center,
                Padding = new Thickness(8, 0, 0, 0),
            };
            _mascots.Add((id, box));
            MascotList.Children.Add(box);
        }

        NoMascots.Visibility = _mascots.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

        // By index, not foreach. DisplayArea.FindAll returns a WinRT-projected
        // IReadOnlyList, and asking one of those for an enumerator under Native AOT throws
        // InvalidCastException out of CsWinRT's Make_IEnumerableObjRef -- the generic
        // instantiation it needs was never generated, and nothing says so until it runs.
        // Indexing goes through a different path and works.
        IReadOnlyList<DisplayArea> areas = DisplayArea.FindAll();
        for (int i = 0; i < areas.Count; ++i)
        {
            DisplayArea area = areas[i];

            // The device name, not the DisplayId. See Native.MonitorDeviceName: the daemon
            // matches the saved list against MONITORINFOEX::szDevice and nothing else, so
            // saving the HMONITOR here matched no monitor and emptied every screen.
            string name = Native.MonitorDeviceName(area.DisplayId.Value);
            if (name.Length == 0)
            {
                Log.Line($"could not name the monitor at {area.OuterBounds.X},{area.OuterBounds.Y}"
                         + "; leaving it out of the list");
                continue;
            }

            CheckBox box = new()
            {
                Content = $"{area.OuterBounds.Width} x {area.OuterBounds.Height}"
                          + $" at {area.OuterBounds.X}, {area.OuterBounds.Y}",
                IsChecked = _settings.Outputs.Count == 0 || _settings.Outputs.Contains(name),
                VerticalContentAlignment = VerticalAlignment.Center,
                Padding = new Thickness(8, 0, 0, 0),
            };
            _outputs.Add((name, box));
            OutputList.Children.Add(box);
        }
    }

    /// <summary>The word beside the switch. Windows puts it to the left of the knob.</summary>
    private void ShowToggleState()
        => FullscreenState.Text = FullscreenToggle.IsOn ? "On" : "Off";

    private static string Pretty(string id)
        => id.Length == 0 ? id : char.ToUpperInvariant(id[0]) + id[1..];

    private void OnApply(object sender, RoutedEventArgs e)
    {
        _settings.PetsPerMascot = (int)Math.Clamp(double.IsNaN(PetsBox.Value) ? 1 : PetsBox.Value, 0, 64);
        _settings.IdleInterval = Math.Clamp(double.IsNaN(IdleBox.Value) ? 0 : IdleBox.Value, 0, 3600);
        _settings.WalkSpeed = SpeedSlider.Value;
        _settings.PauseForFullscreen = FullscreenToggle.IsOn;

        // All ticked is saved as the empty list, so that installing a fourth mascot later
        // brings it in rather than leaving it out of a list written before it existed.
        List<string> mascots = [.. _mascots.Where(m => m.Box.IsChecked == true).Select(m => m.Id)];
        _settings.Mascots = mascots.Count == _mascots.Count ? [] : mascots;

        List<string> outputs = [.. _outputs.Where(o => o.Box.IsChecked == true).Select(o => o.Name)];
        _settings.Outputs = outputs.Count == _outputs.Count ? [] : outputs;

        if (!_settings.Save())
        {
            Report(InfoBarSeverity.Error, $"Could not write {Settings.Path}");
            return;
        }

        // The daemon may not be running -- somebody can open this from the Start menu -- and
        // that is not a failure. The file is saved either way and will be read at startup.
        Report(Daemon.Send("reload")
                   ? InfoBarSeverity.Success
                   : InfoBarSeverity.Informational,
               Daemon.IsRunning() ? "Saved." : "Saved. DragonPerch is not running.");
    }

    private void Report(InfoBarSeverity severity, string message)
    {
        Status.Severity = severity;
        Status.Message = message;
        Status.IsOpen = true;
    }

    private void OnClose(object sender, RoutedEventArgs e) => Close();
}
