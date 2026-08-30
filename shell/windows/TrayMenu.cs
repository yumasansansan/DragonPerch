// SPDX-License-Identifier: GPL-3.0-or-later

using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics;

namespace DragonPerch.Shell;

/// <summary>
/// The tray icon's context menu, as a real WinUI 3 <see cref="MenuFlyout"/>.
/// </summary>
/// <remarks>
/// A flyout has to be shown from somewhere, so there is a host window: one pixel, no
/// border, no taskbar button, positioned at the cursor. It is never seen -- the flyout
/// covers it and the presenter gives it nothing to draw -- and it exists only because
/// XAML has no way to put a menu on the screen without an element to anchor it to.
///
/// The window is hidden rather than closed when the menu goes away, and this object
/// outlives the menu. Building it is most of what a cold WinUI process spends its time on,
/// and the second right-click should be immediate.
/// </remarks>
internal sealed class TrayMenu
{
    // Segoe Fluent Icons, by code point rather than by pasting the glyphs. They live in the
    // private use area, where a stray editor or a diff viewer is free to mangle them.
    private const string GlyphPlay = "";
    private const string GlyphPause = "";
    private const string GlyphSettings = "";
    private const string GlyphQuit = "";

    private Window? _host;
    private Grid? _anchor;
    private MenuFlyout? _flyout;
    private bool _pending;
    private SettingsWindow? _settings;

    /// <summary>Shows the menu at a point in physical screen pixels.</summary>
    /// <param name="paused">Which way round the pause item should read.</param>
    public void ShowAt(int x, int y, bool paused)
    {
        EnsureHost();

        // One pixel at the cursor. The flyout is positioned relative to this, and Windows
        // does the work of keeping it on screen and flipping it near an edge -- which is
        // most of what makes a menu opened next to the taskbar feel right.
        //
        // Resized after Activate() as well as before it. The first activation of a WinUI
        // window applies a default size, so sizing it beforehand is thrown away and what
        // appears is a small empty window sitting next to the menu -- which is exactly what
        // it looked like.
        _host!.AppWindow.MoveAndResize(new RectInt32 { X = x, Y = y, Width = 1, Height = 1 });
        _host.Activate();
        _host.AppWindow.MoveAndResize(new RectInt32 { X = x, Y = y, Width = 1, Height = 1 });
        Log.Line($"host: {_host.AppWindow.Size.Width}x{_host.AppWindow.Size.Height} "
                 + $"at {_host.AppWindow.Position.X},{_host.AppWindow.Position.Y}");

        // Without this the flyout appears behind whatever was focused and does not dismiss
        // when clicked away from -- the same reason a Win32 tray menu needs
        // SetForegroundWindow before TrackPopupMenuEx.
        _ = Native.SetForegroundWindow(
            Microsoft.UI.Win32Interop.GetWindowFromWindowId(_host.AppWindow.Id));

        // Activate() does not finish putting the content into a tree before it returns, and
        // a flyout cannot be shown from an element with no XamlRoot -- it throws "This
        // element does not have a XamlRoot", which on the very first right-click looks like
        // the menu simply not appearing. So the first showing waits for Loaded; every one
        // after it is immediate, because the host is kept rather than rebuilt.
        if (_anchor!.XamlRoot is null)
        {
            Log.Line("flyout: waiting for the host to be loaded");
            _pending = paused;
            _anchor.Loaded += OnAnchorLoaded;
            return;
        }

        Show(paused);
    }

    /// <summary>
    /// Opens the settings window, or brings the open one forward.
    /// </summary>
    /// <remarks>
    /// One window, kept rather than recreated: a second copy of a settings window is two
    /// views of one file that can disagree, and whichever is applied last wins silently.
    /// </remarks>
    public void ShowSettings()
    {
        // Caught rather than allowed to escape: this runs from a XAML event handler, where
        // an exception takes the whole process with it -- and the process is the tray menu.
        // A settings window that fails to open should leave the menu working.
        try
        {
            if (_settings is null)
            {
                Log.Line("settings: opening");
                _settings = new SettingsWindow();
                _settings.Closed += (_, _) =>
                {
                    Log.Line("settings: closed");
                    _settings = null;
                };
            }

            _settings.Activate();
            _ = Native.SetForegroundWindow(
                Microsoft.UI.Win32Interop.GetWindowFromWindowId(_settings.AppWindow.Id));
            Log.Line("settings: shown");
        }
        catch (Exception e)
        {
            Log.Failure("opening the settings window", e);
            _settings = null;
        }
    }

    private void OnAnchorLoaded(object sender, RoutedEventArgs e)
    {
        _anchor!.Loaded -= OnAnchorLoaded;
        Show(_pending);
    }

    private void Show(bool paused)
    {
        // The previous menu, if it is somehow still up. Showing a second flyout closes the
        // first one anyway; doing it here means it happens before the new one exists, so
        // the Closed handler below can tell the two apart.
        _flyout?.Hide();

        // Rebuilt each time rather than kept and patched: it is four items, and a menu
        // whose labels are assembled from the state passed in cannot get out of step with
        // that state.
        _flyout = BuildFlyout(paused);
        _flyout.XamlRoot = _anchor!.XamlRoot;

        Log.Line("flyout: showing");
        _flyout.ShowAt(_anchor, new FlyoutShowOptions
        {
            Placement = FlyoutPlacementMode.TopEdgeAlignedLeft,
            ShowMode = FlyoutShowMode.Standard,
        });
    }

    private void EnsureHost()
    {
        if (_host is not null)
        {
            return;
        }

        _anchor = new Grid { Width = 1, Height = 1 };
        _host = new Window { Content = _anchor };

        // Exactly what this is: borderless, always on top, and not something alt-tab should
        // offer. CreateForContextMenu is the App SDK saying so in one call rather than six
        // style bits.
        OverlappedPresenter presenter = OverlappedPresenter.CreateForContextMenu();
        presenter.IsAlwaysOnTop = true;
        presenter.SetBorderAndTitleBar(false, false);
        _host.AppWindow.SetPresenter(presenter);
        _host.AppWindow.IsShownInSwitchers = false;

        // Closing the host would mean quitting, because it is the only window this program
        // has. Hide it instead and let App decide when the process should go.
        _host.AppWindow.Closing += (_, e) =>
        {
            e.Cancel = true;
            _host!.AppWindow.Hide();
        };

        // Escape and clicking away are handled by the flyout itself. This is the third way
        // out -- something else taking the foreground while the menu is up, which is not a
        // dismissal the flyout hears about.
        _host.Activated += (_, e) =>
        {
            if (e.WindowActivationState == WindowActivationState.Deactivated)
            {
                _flyout?.Hide();
            }
        };
    }

    /// <summary>
    /// Fluent 2's body font, as a value on the item rather than a resource under it.
    /// </summary>
    /// <remarks>
    /// App.xaml replaces ContentControlThemeFontFamily, which is enough for every control in
    /// the settings window. It is not enough here: MenuFlyoutItem's default style does not
    /// reference that resource, so the items kept the XAML default, which resolves to the
    /// system UI font for the current language. Measured, not assumed: the item reported
    /// Meiryo UI with the resource already overridden. A local value is the one thing a
    /// default style cannot outrank.
    ///
    /// Constructed rather than read out of Application.Current.Resources, and the name is
    /// deliberately repeated from App.xaml. Casting what that dictionary returns to
    /// FontFamily is a QueryInterface, and under Native AOT it answers E_NOINTERFACE and
    /// takes the process with it: the menu logged that it was waiting for its host and then
    /// simply was not there, and the daemon fell back to the Win32 menu because nothing
    /// answered it. It only happens in a published build, which is why it survived being
    /// measured -- the measuring was done on `dotnet build` output. Same family as the
    /// projected IReadOnlyList in SettingsWindow.
    /// </remarks>
    private static readonly FontFamily MenuFont = new("Segoe UI Variable Text");

    /// <summary>
    /// One pixel up, so the glyph lines up with the label rather than with the row.
    /// </summary>
    /// <remarks>
    /// Centring a line of text centres its line box, and a line box has the descender space
    /// underneath it whether or not the word has a descender -- so the part anybody actually
    /// sees, cap height down to baseline, ends up sitting slightly above the middle. An icon
    /// glyph does not have that asymmetry and lands on the middle exactly. Measured on the
    /// Pause item, whose label has neither an ascender nor a descender to confuse it: the
    /// text was a pixel above the icon.
    ///
    /// The icon is moved rather than the label, because the label is the thing being read
    /// and because moving it would mean restyling the item's template.
    /// </remarks>
    private static readonly Thickness IconNudge = new(0, -1, 0, 0);

    private MenuFlyout BuildFlyout(bool paused)
    {
        MenuFlyoutItem pause = new()
        {
            Text = paused ? "Resume" : "Pause",
            Icon = new FontIcon { Glyph = paused ? GlyphPlay : GlyphPause, Margin = IconNudge },
            FontFamily = MenuFont,
        };
        pause.Click += (_, _) => _ = Daemon.Send("toggle-pause");

        MenuFlyoutItem settings = new()
        {
            Text = "Settings…",
            Icon = new FontIcon { Glyph = GlyphSettings, Margin = IconNudge },
            FontFamily = MenuFont,
        };
        settings.Click += (_, _) => ShowSettings();

        MenuFlyoutItem quit = new()
        {
            Text = "Quit DragonPerch",
            Icon = new FontIcon { Glyph = GlyphQuit, Margin = IconNudge },
            FontFamily = MenuFont,
        };
        quit.Click += (_, _) => _ = Daemon.Send("quit");

        MenuFlyout flyout = new()
        {
            // The menu is much larger than the one-pixel window it hangs off, so it has to
            // be allowed its own top-level window rather than being clipped to that one.
            ShouldConstrainToRootBounds = false,
            SystemBackdrop = new Microsoft.UI.Xaml.Media.DesktopAcrylicBackdrop(),
        };
        flyout.Items.Add(pause);
        flyout.Items.Add(settings);
        flyout.Items.Add(new MenuFlyoutSeparator());
        flyout.Items.Add(quit);


        flyout.Closed += (sender, _) =>
        {
            // Only when it is the menu currently on screen. Opening a second menu closes
            // the first, and letting the first one's handler run would hide the host
            // window the second one is anchored to -- which showed up as a menu that
            // appeared and vanished in the same frame, every time but the first.
            if (!ReferenceEquals(sender, _flyout))
            {
                Log.Line("flyout: an older menu closed; leaving the host alone");
                return;
            }

            Log.Line("flyout: closed");
            _host?.AppWindow.Hide();
        };

        return flyout;
    }
}
