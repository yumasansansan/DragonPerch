<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Running it

The command line, the tray icon, and the diagnostic modes. What to change rather than
how to run it is in [Settings](settings.md).

```bash
./build/windows-x64/src/win/Debug/dragonperch.exe --probe-composition --hold
```

```bash
./build/windows-x64/src/win/Debug/dragonperch.exe --dump-world --hold
```

```bash
./build/windows-x64/src/win/Debug/dragonperch.exe --pets 6
```

```bash
./build/windows-x64/src/win/Debug/dragonperch.exe --pause
./build/windows-x64/src/win/Debug/dragonperch.exe --stop
```

On Linux the geometry comes from the compositor, and DragonPerch arranges that itself: it
finds the KWin script and asks KWin to run it at every startup, so there is nothing to
install or enable first. [What you need](requirements.md) says where it looks, which
compositors work at all, and what `kwin/install.sh` is for.

```bash
./build/linux-x64/src/linux/Debug/dragonperch-wl --dump-world --hold
```

```bash
./build/linux-x64/src/linux/Debug/dragonperch-wl --pets 6
```

The first draws an opaque quad, a half-transparent one overlapping it, and an outline,
through DirectComposition on a click-through window. The second prints the walkable edges
and reprints them whenever the desktop changes — drag a window and watch the numbers follow
its title bar. The third is the app: it loads every mascot in `assets/` and shares the pets
out between them, so that is six pets, two of each.

**The diagnostic modes are Debug-only.** `--probe-composition`, `--dump-world`,
`--self-test` and `--export-placeholder` are compiled out of a release build, which is what
the packages and the nightlies are. They are not dead weight — every hard bug in this
project was found by one of them — so the switch is separate from the configuration:

```bash
cmake --preset windows-x64 -D DRAGONPERCH_DIAGNOSTICS=ON
```

That gives a *release* binary with the diagnostics in, which is what to build when a
shipped build misbehaves. A Debug build has different timing and a different Direct2D
layer, so it answers a different question.

There is a tray icon on both platforms, and it is the ordinary way to stop DragonPerch:
right-click it for Pause, Settings and Quit. On Windows it is `Shell_NotifyIcon`; on Linux
it is StatusNotifierItem, where the menu is *described* rather than drawn — Plasma builds it
from the labels, in Breeze, following the user's theme with no code on our side.

The menu the two platforms draw is meant to be the native one, and on Windows that now
means a real WinUI 3 `MenuFlyout` rather than something shaped like one. It lives in
`DragonPerch.Shell.exe`, a separate program in `shell/windows/`, for a reason that was
measured rather than assumed: initialising XAML costs a process about 50 MB of private
bytes permanently, and closing it again returns none of it (docs/plan.md §13.3 has the
numbers). So the toolkit goes somewhere it can be started on demand and killed without the
pets noticing, and `dragonperch.exe` stays a 2 MB Win32 process with no App SDK anywhere
near it.

The daemon starts the shell when the pointer arrives over the tray icon, which buys the
couple of hundred milliseconds a cold WinUI process needs before the button is pressed. If
the shell is not installed, has not finished starting, or has been killed, the daemon shows
its own `TrackPopupMenuEx` menu instead — it never waits for one, because a menu that
arrives half a second after the click reads as a hang. **The shell is optional in the
strongest sense: the daemon runs, and is fully usable, with no trace of it on the disk.**

Building it needs the .NET 10 SDK and is not part of the CMake build, because CMake cannot
sensibly build a WinUI project:

```
dotnet publish shell/windows/DragonPerch.Shell.csproj -c Release -o <somewhere>
```

Copy the result next to `dragonperch.exe`. It is self-contained and compiled with Native
AOT: about 62 MB once the linker's symbols are dropped, and a cold start of roughly 70 ms
measured through the tray icon. Still large enough that CI ships it as its own zip rather
than in with the pets.

`--stop`, `--pause`, `--resume` and `--reload` all go through one control interface — a
message-only window answering `WM_COPYDATA` on Windows, `org.dragonperch.Control` on the
session bus on Linux. Neither needs a thread of its own: Windows already pumps messages for
the overlay windows, and Linux already processes the bus the KWin reports arrive on. The
tray icon and the settings program will be two more callers of the same four commands.

Pausing stops the simulation and stops drawing; the overlays stay exactly as they are,
showing the last frame. Rebuilding them on resume would be a second path through the window
and surface code, which is the part of this program that has been wrong most often.

`--stop` in particular is there because Ctrl+C on Windows nearly works and cannot be relied
on. This is a
GUI-subsystem binary, so the shell does not wait for it and the prompt comes straight back;
whether the Ctrl+C typed at that prompt becomes a console event is then up to the shell.
`cmd` sends one, PowerShell 7 does not — PSReadLine handles the key itself. Closing the
console works too. On Linux, Ctrl+C works.

`--dump-world` on Linux is the same idea and the thing to run first on a new machine: it
prints what KWin says and starts no renderer at all, so if the numbers follow a dragged
window then the hard half works and anything still wrong on screen belongs to the renderer.

Diagnostics go to stdout **and** to `dragonperch.log` beside the executable, because a
GUI-subsystem binary cannot rely on having a console.
