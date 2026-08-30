<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Settings

One file, three programs that read or write it, and a rule about what happens when a line
in it is wrong.

## Where it is

| | |
|---|---|
| Windows | `%APPDATA%\DragonPerch\dragonperchrc` |
| Linux | `$XDG_CONFIG_HOME/dragonperch/dragonperchrc`, or `~/.config` below it |

The name is KConfig's convention. It is INI, and it is meant to be edited by hand as well
as by the settings programs.

## What is in it

```ini
[DragonPerch]
pets-per-mascot = 1
mascots =
walk-speed = 42.00
idle-interval = 9.00
outputs =
pause-for-fullscreen = true
```

| Key | Range | Means |
|---|---|---|
| `pets-per-mascot` | 0 – 64 | How many of *each* mascot. Three mascots at 2 is six pets. |
| `mascots` | pack ids, comma separated | Which ones walk. **Empty means all of them.** |
| `walk-speed` | 1 – 1000 | Pixels per second. The walk cycle was drawn against 42. |
| `idle-interval` | 0 – 3600 | Mean seconds between spontaneous pauses. 0 stops them. |
| `outputs` | monitor names, comma separated | Where they are allowed. **Empty means all of them.** |
| `pause-for-fullscreen` | `true` / `false` | Hide on a monitor showing something full screen. |

Empty meaning "all of them" is deliberate, and it is why turning every tick box off saves
an empty list rather than nothing: a mascot installed next year should be included rather
than left out of a list written before it existed.

The monitor names are the ones the system reports — `\.\DISPLAY1` on Windows, the
connector name such as `DP-1` on Wayland. They are not display names anybody chose, and
getting them from somewhere else is [how the Windows settings window once saved an
identifier the daemon could never match](plan.md), which emptied every screen at once.

## What happens to a line it cannot read

**A bad line costs that line and nothing else.** A value out of range is clamped, a value
that is not a number keeps its default, and a line that is not `key = value` at all is
skipped. Where the same thing is said twice — a repeated key, or the section opened again —
the later answer wins, as INI conventionally means and as KConfig reads it.

That was not always true: the daemon used to answer a single unreadable line by putting
*every* setting back to its default, which meant it and the settings program could disagree
about what the user had chosen with neither of them saying so. The two are now checked
against each other over a set of deliberately awkward files.

Sprite pack definitions are read by the same parser with the opposite setting: a pack that
is only half readable describes the wrong sprites, so it is refused rather than guessed at.

## The settings programs

### Windows

The tray menu's **Settings** item, which is part of `DragonPerch.Shell.exe` — the same
optional WinUI 3 program that draws the Fluent tray menu. See
[Running it](running.md) for what it is and why it is a separate process.

Both menus open it. The Fluent one shows the window itself; the daemon's own Win32 menu —
the one drawn when the shell is missing or was too slow for that click — asks a running
shell for it, or starts one when there is none. When no shell is installed the item is
greyed, on the same reasoning as the Linux one below: an item that is present and does
nothing is worse than one that is visibly not available.

### Linux

A KDE Config Module, `kcm_dragonperch`. The tray menu's **Settings** item runs

```bash
kcmshell6 kcm_dragonperch
```

and the same module appears in System Settings, where somebody using KDE would look for it
first. It is greyed in the menu when there is no `kcmshell6` to run it with.

It is built only when asked for, because it is the only part of this project that wants Qt
and KDE Frameworks and the daemon has to keep building on a machine with neither:

```bash
cmake --preset linux-x64 -D DRAGONPERCH_BUILD_KCM=ON
```

The packages are built with it on, so installing the `.deb` gets it.

### Both, and the daemon

Saving writes the whole file and then asks the daemon to read it again — `WM_COPYDATA` on
Windows, `org.dragonperch.Control1.Reload` on the session bus on Linux — so a change is on
the screen before the window has finished closing. Nothing is applied without Apply being
pressed: a settings window that acts on every keystroke would have the pets respawning
while somebody is still typing a number.

Changing a speed is applied to the pets where they stand. Changing which mascots there are
spawns them again, because there is no other way to do that one.

`--pets N` on the command line overrides `pets-per-mascot` for that run without touching
the file.
