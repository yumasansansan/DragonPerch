<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# What you need

Whether it will run on your machine, and the one thing on Linux that is worth understanding
before it does not.

*[日本語版はこちら](ja/requirements.md)*

## Windows

| | |
|---|---|
| **Windows 10 or 11** | The pets, the tray icon, the command line |
| **Windows 11** | …and the Fluent tray menu and settings window |

Nothing else. No .NET runtime, no redistributable: `dragonperch.exe` is a 2 MB Win32
program with everything it needs inside it.

The Fluent half is a separate program and installing it is optional — [Running
it](running.md) says what it is and why it is separate. It asks for Windows 11 because Segoe
UI Variable and the rest of what Fluent 2 specifies arrived with it.

Without it you get the daemon's own Win32 menu, which does everything the Fluent one does
except look like it: **Settings** opens the same window when the shell is installed, and is
greyed when it is not. The pets, the tray icon and the command line do not change.

Only Windows 11 is actually tested. The daemon's manifest declares Windows 10 as well, and
nothing in it needs 11.

## Linux

**Wayland only**, and your compositor has to provide two separate things.

| | Overlay | Window positions | |
|---|---|---|---|
| **KDE Plasma 6 (Wayland)** | ✔ layer shell | ✔ KWin script | Everything works |
| wlroots — Sway, Hyprland | ✔ layer shell | ✘ not written | Pets appear, with only the bottom of the screen to walk on |
| GNOME (Mutter) | ✘ no layer shell | — | Will not start |
| X11, any desktop | — | — | Not a target |

**The overlay** needs `zwlr_layer_shell_v1`. Every wlroots compositor implements it and so
does KWin; Mutter has declined to, and there is no other way to put a click-through
full-screen surface over a Wayland desktop. Without it DragonPerch stops with an error
rather than drawing nothing and saying nothing.

**The window positions** are the harder half, and the reason the program is shaped the way
it is. A Wayland client is not allowed to know where any other client's windows are, so
something inside the compositor has to tell it. On Plasma that is the KWin script below.
Sway and Hyprland could answer through `swaymsg` and `hyprctl` — nobody has written it yet.
GNOME would need a Shell extension.

Without any of them the pets still run. They just have nothing to stand on except the
bottom of the screen, and the daemon says so when it notices.

X11 is deliberately not a target: another world provider, a worse frame clock, and a third
platform to carry every feature after this one, for a session type Plasma is winding down.
[The plan](plan.md) has the argument.

## The KWin script

This is the part the documentation used to get wrong, so it is worth being exact.

**In normal use there is nothing to install and nothing to enable.** Every time it starts,
DragonPerch finds the script and asks KWin to run it, over KWin's own scripting interface on
the session bus. It looks in this order:

1. `../share/kwin/scripts/` relative to the binary — an unpacked tarball, and `/usr/share`
   for a package in `/usr/bin`
2. `$XDG_DATA_HOME/kwin/scripts/` — where `kwin/install.sh` puts it
3. `/usr/share/kwin/scripts/` and `/usr/local/share/kwin/scripts/`
4. up the source tree from the binary — a build directory in a checkout

So the `dragonperch-kde` package works as soon as it is installed, an unpacked tarball works
where it was unpacked, and a freshly built checkout works out of the build directory. The
log line to look for is `kwin: asked it to re-run …`.

It has to be done this way round. A KWin script is a statement list that runs once when the
compositor loads it — at login, long before DragonPerch exists — and after that it only
speaks when a window changes. So the client asks the compositor to re-run it at a moment
when the client is listening.

### Then what is the tick-box in System Settings for?

> System Settings → Window Management → KWin Scripts → *DragonPerch geometry*

Ticking it makes **KWin** load the script at login, whether or not DragonPerch is running.
That is belt and braces rather than a requirement: the script is then already reporting when
the daemon starts, instead of being loaded a moment afterwards. The `dragonperch-kde`
package deliberately leaves it unticked — the metadata says `EnabledByDefault: false` and
nothing in the package writes to your `kwinrc`. A program that starts running code inside
somebody's compositor because a dependency pulled it in would deserve everything it got.

`kwin/install.sh` does the same thing from a checkout: it copies the script to
`~/.local/share/kwin/scripts/`, ticks the box by writing the `kwinrc` entry, and reloads
KWin's scripting so it takes effect without logging out. It is in the repository, not in the
tarball, and you do not need it to run DragonPerch.

### If you have run install.sh and then installed a package

There are then two copies, and the two loaders do not choose the same one. DragonPerch
looks beside its own binary first, so a package in `/usr/bin` gets the packaged copy; KWin's
own loader prefers the one under your home directory, so that is what runs at login until
DragonPerch replaces it. Right while developing, wrong after an upgrade. The daemon names
both rather than quietly picking one, and says so again if the script that answers speaks a
different report format. If a package upgrade seems to have changed nothing, this is why:

```bash
rm -rf ~/.local/share/kwin/scripts/dragonperch-geometry
```

### Checking it is talking

```bash
dragonperch-wl --dump-world --hold
```

Drag a window and watch the numbers follow the title bar. If they do, the difficult half
works. `--dump-world` is a diagnostic, so it is not in a release build; [Running
it](running.md) says how to build a release binary that keeps the diagnostics. The script's
own output goes to the compositor's journal:

```bash
journalctl --user -f -t kwin_wayland
```
