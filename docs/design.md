<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# How DragonPerch is put together

The shape of the program, and the one constraint that decided it. For what is built
next and why, see [the plan of record](plan.md).

## The one thing that shapes the design

Drawing sprites is easy everywhere. **Finding out where other applications' windows are is
not**, and on Wayland it is deliberately impossible for a normal client. So the simulation
never learns what an `HWND` or a `wl_surface` is: every backend flattens whatever it can
discover into a list of horizontal line segments, and the physics walks those.

| | Overlay surface | Other windows' geometry |
|---|---|---|
| Windows | DirectComposition on a layered host window | `EnumWindows` + DWM |
| Wayland / KWin (Plasma) | `zwlr_layer_shell_v1` | a **KWin script** over D-Bus |
| Wayland / wlroots | `zwlr_layer_shell_v1` | `swaymsg` / `hyprctl` |
| Wayland / GNOME | ✘ no layer shell | ✘ — would need a Shell extension |

X11 is deliberately not a target: it would need a second world provider, a worse frame
clock, and a third platform to carry through every feature from here on, for a session type
Plasma is retiring. See [docs/plan.md](plan.md) §13.1.

The KWin script is loaded by the daemon rather than by the user: it is found beside the
executable or in the system's data directories, and handed to KWin's own `loadScript` over
D-Bus at every startup. Nothing has to be installed or ticked for that to work. [What you
need](requirements.md) is the page to read on the day it does not.

## Layout

```
src/core/       portable. No OS headers — CI builds this alone to keep it that way.
src/win/        Windows head: DirectComposition + Direct2D + Win32.
src/linux/      Linux head: layer-shell + EGL/GL, sd-bus, StatusNotifierItem.
kwin/           KWin script. Runs inside the compositor, pushes geometry over D-Bus.
shell/windows/  the Fluent tray menu and settings window. C#, WinUI 3, Native AOT,
                optional: the daemon works with none of it on disk.
kcm/            the KDE settings module. Qt6 and KF6, built only with -D DRAGONPERCH_BUILD_KCM=ON.
lang/           the translation catalogues. One string table, shared by every program here.
tests/          Catch2. The physics with no compositor, no windows and no platform.
fuzz/           libFuzzer targets, built with -D DRAGONPERCH_SANITIZE=ON.
cmake/          every compiler flag, the packaging, and the Clang/LLD discovery.
external/       upstream Wayland protocol XML, as submodules.
tools/          the sprite-pack generators. Inkscape and Pillow, run by hand, not by CMake.
packaging/      the .desktop file. Everything else about packaging is cmake/Packaging.cmake.
assets/         one directory per mascot. CC BY-SA 4.0, not GPL — see assets/README.md.
docs/plan.md    the plan of record, including findings that cost real time to establish.
```

Paths in the tree above are relative to the repository root.
