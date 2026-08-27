<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# DragonPerch

**Konqi and friends romp across your window title bars and panels — a cross-platform
XPenguins for Windows and Linux (X11/Wayland).**

Dragons walk along the top edge of your windows, sit on your taskbar, and fall off when you
drag the window out from under them. C++23, GPU rendering, as close to the platform as
practical.

> Status: **milestone 1.** The build works on both platforms and the Windows GPU path is
> proven. Nothing walks yet. See [docs/plan.md](docs/plan.md).

---

## The one thing that shapes the design

Drawing sprites is easy everywhere. **Finding out where other applications' windows are is
not**, and on Wayland it is deliberately impossible for a normal client. So the simulation
never learns what an `HWND` or a `wl_surface` is: every backend flattens whatever it can
discover into a list of horizontal line segments, and the physics walks those.

| | Overlay surface | Other windows' geometry |
|---|---|---|
| Windows | DirectComposition on a layered host window | `EnumWindows` + DWM |
| Linux / X11 | override-redirect ARGB + XShape | EWMH |
| Wayland / KWin (Plasma) | `zwlr_layer_shell_v1` | a **KWin script** over D-Bus |
| Wayland / wlroots | `zwlr_layer_shell_v1` | `swaymsg` / `hyprctl` |
| Wayland / GNOME | ✘ no layer shell | ✘ — would need a Shell extension |

## Building

CMake is the single source of truth. The Visual Studio solution is generated from it.

```bash
cmake --preset windows-x64
```

That writes `build/windows-x64/DragonPerch.slnx`, which opens and debugs in Visual Studio
2026 as usual. Project properties changed inside VS do not persist — CMake regenerates
them — so build settings belong in `CMakeLists.txt`.

```bash
cmake --build --preset windows-x64-debug
```

On Linux, or for faster command-line iteration on Windows:

```bash
cmake --preset linux-x64 && cmake --build --preset linux-x64-debug
```

## Trying it

```bash
./build/windows-x64/src/win/Debug/dragonperch.exe --probe-composition --hold
```

Milestone 1: draws an opaque quad, a half-transparent one overlapping it, and an outline,
through DirectComposition on a click-through window. Diagnostics go to stdout **and** to
`dragonperch.log` beside the executable, because a GUI-subsystem binary cannot rely on
having a console.

## Layout

```
src/core/     portable. No OS headers — CI builds this alone to keep it that way.
src/win/      Windows head: DirectComposition + Direct2D + Win32.
src/linux/    Linux head: layer-shell + EGL/GL.            (milestone 6)
kwin/         KWin script. Runs inside the compositor, pushes geometry over D-Bus.
protocols/    vendored Wayland XML.
assets/konqi/ artwork. CC BY-SA 4.0, not GPL.
docs/plan.md  the plan of record, including findings that cost real time to establish.
```

## Licensing

Code is `GPL-3.0-or-later`.

Artwork is **not**. Konqi and the other KDE mascots are the work of the KDE community under
`CC-BY-SA-4.0`. Keep them that way; there is no reason to relicense data that is loaded at
runtime rather than linked.
