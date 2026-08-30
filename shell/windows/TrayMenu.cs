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
    // Segoe Fluent Icons, written as escapes rather than pasted in. They live in the private
    // use area, where a stray editor or a diff viewer is free to mangle them -- which is what
    // the line above this used to claim while the glyphs themselves sat in the file.
    //
    // E7E8 PowerButton for Quit, which began as a question about a cross that looked a little
    // up and to the left of where it should be. It was: E711 ChromeClose is the title bar's
    // close button, drawn small and light to suit one, and it measured 11 pixels of ink beside
    // its neighbours' 14 and 16. An odd width cannot be centred on a half pixel boundary, so
    // it sat half a pixel up and half a pixel left of the other two.
    //
    // E8BB Cancel fixes the centring at 16 by 16, the same as the gear and with less ink than
    // either neighbour -- but a cross fills its box corner to corner and reads larger than a
    // gear of the same size, which is exactly what it looked like. There is no cross in
    // between: every one in this font measures either 11 or 16.
    //
    // A power symbol is 14 by 14, the same as the pause bars, and lighter than both of them.
    // It also says the right thing: a cross closes a window, and this ends the program.
    private const string GlyphPlay = "\uE768";
    private const string GlyphPause = "\uE769";
    private const string GlyphSettings = "\uE713";
    private const string GlyphQuit = "\uE7E8";

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

        // See Native.ShowTheArrowCursor. The menu opens under a pointer that is not going to
        // move, over a window that does not set the cursor, so nothing else is going to.
        Native.ShowTheArrowCursor();
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

    /// <summary>An icon for a menu item, raised by <paramref name="up"/> pixels.</summary>
    /// <remarks>
    /// Every one of them needs raising by at least one. Centring a line of text centres its
    /// line box, and a line box carries the descender space underneath it whether or not the
    /// word has a descender -- so the part anybody actually sees, cap height down to
    /// baseline, sits slightly above the middle. An icon glyph has no such asymmetry and
    /// lands on the middle exactly. Measured on the Pause item, whose label has neither an
    /// ascender nor a descender to confuse the reading: the text was a pixel above the icon.
    ///
    /// The icon moves rather than the label, because the label is the thing being read and
    /// because moving it would mean restyling the item's template.
    ///
    /// The amount is per icon because the glyphs do not agree with each other either. The
    /// distance from the label's cap height to the centre of the glyph beside it came out at
    /// 4.5 pixels for the pause bars and the gear and 5.5 for the power symbol, whose ink
    /// sits a pixel lower inside its own box.
    /// </remarks>
    private static FontIcon MenuIcon(string glyph, int up)
        => new() { Glyph = glyph, Margin = new Thickness(0, -up, 0, 0) };

    /// <summary>
    /// The height a menu item has when WinUI is being generous with it, in effective pixels.
    /// </summary>
    /// <remarks>
    /// WinUI keeps two paddings for a menu item and picks between them by what opened the
    /// menu: MenuFlyoutItemThemePadding is 11,8,11,9 and MenuFlyoutItemThemePaddingNarrow is
    /// 11,4,11,5. Read out of the theme at runtime. Eight pixels an item apart, and measured
    /// on screen the item is 38 pixels tall on the roomy one and 30 on the narrow -- which is
    /// the difference between the first right-click and every one after it.
    ///
    /// Two attempts at this failed and are worth writing down, because both looked right.
    /// Setting the item's Padding does not replace the template's: the template puts the
    /// state's value on its own layout root and the control's Padding inside that, so both
    /// variants grew by the same amount and stayed eight pixels apart. Forcing the visual
    /// state with GoToState does replace it, and does work when the menu is driven from a
    /// script -- but not from the tray icon, where something sets it back afterwards.
    ///
    /// A minimum height argues with none of that. Whatever padding the template settles on,
    /// the item cannot be shorter than this, so the two cases meet at the taller one.
    /// </remarks>
    private const double RoomyItemHeight = 38;

    private MenuFlyout BuildFlyout(bool paused)
    {
        MenuFlyoutItem pause = new()
        {
            Text = paused ? "Resume" : "Pause",
            Icon = MenuIcon(paused ? GlyphPlay : GlyphPause, 1),
            MinHeight = RoomyItemHeight,
            VerticalContentAlignment = VerticalAlignment.Center,
            FontFamily = MenuFont,
        };
        pause.Click += (_, _) => _ = Daemon.Send("toggle-pause");

        MenuFlyoutItem settings = new()
        {
            Text = "Settings…",
            Icon = MenuIcon(GlyphSettings, 1),
            MinHeight = RoomyItemHeight,
            VerticalContentAlignment = VerticalAlignment.Center,
            FontFamily = MenuFont,
        };
        settings.Click += (_, _) => ShowSettings();

        MenuFlyoutItem quit = new()
        {
            Text = "Quit DragonPerch",
            Icon = MenuIcon(GlyphQuit, 2),
            MinHeight = RoomyItemHeight,
            VerticalContentAlignment = VerticalAlignment.Center,
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
