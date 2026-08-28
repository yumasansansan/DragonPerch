<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# DragonPerch

**Konqi and friends romp across your window title bars and panels — a cross-platform
XPenguins for Windows and Wayland.**

Dragons walk along the top edge of your windows, sit on your taskbar, and fall off when you
drag the window out from under them. C++23, GPU rendering, as close to the platform as
practical.

> Status: **Windows works.** Konqi, Katie and Kori walk on your title bars and taskbar,
> drawn on the GPU from KDE's own artwork; clicks pass through them, and they get out of the
> way of full-screen apps.
>
> **Linux works too.** The Wayland head draws through EGL on a layer-shell overlay, and a
> KWin script tells it where the windows are — verified on Plasma 6 under llvmpipe. A tray
> icon and settings are next; see [docs/plan.md](docs/plan.md).

---

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
Plasma is retiring. See [docs/plan.md](docs/plan.md) §13.1.

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

On Linux, the protocol definitions are submodules and the head has dependencies:

```bash
git submodule update --init --depth 1
```

```bash
sudo apt install clang-22 cmake ninja-build libwayland-bin libwayland-dev libwayland-egl-backend-dev libegl-dev libgles-dev libpng-dev libsystemd-dev pkg-config
```

```bash
cmake --preset linux-x64 && cmake --build --preset linux-x64-debug
```

Use the preset rather than a hand-written `cmake -G Ninja`, and note the environment
variable for the C compiler is `CC`, not `C` — set the wrong one and CMake silently picks
whatever `cc` happens to be while using Clang for C++.

The Linux preset names `clang-22` rather than `clang`, because on Ubuntu 26.04 plain
`clang` is 21. Both are on the image, so nothing is installed — but picking up a different
compiler than intended is the kind of thing that surfaces much later as a confusing
diagnostic, so it is stated rather than inferred.

`external/` holds `wayland-protocols` and `wlr-protocols` as submodules rather than copies
of the two XML files, so that where each came from is recorded and updating is one command.
`wayland-scanner` turns them into C at build time — a Wayland protocol is a data file, not
a library, so there is nothing to link against.

Ninja is the generator on Linux because there is no solution to open there -- it is a build
executor, the counterpart to MSBuild, and CMake writes its input. On Windows the Visual
Studio generator does that job, so there is deliberately no Ninja preset for Windows.

### Compiler and linker flags

All of them live in [cmake/CompilerOptions.cmake](cmake/CompilerOptions.cmake), on one
interface target that every real target links. Nothing else in the tree sets a flag.

Release-only flags go inside `$<$<CONFIG:Release>:...>`. That is not stylistic: the Visual
Studio and Ninja Multi-Config generators pick the configuration at *build* time, so testing
`CMAKE_BUILD_TYPE` at configure time silently does nothing.

MSVC splits its flags across two tools, and a linker flag handed to the compiler is ignored
rather than rejected:

| | tool | set with |
|---|---|---|
| `/O2 /Oi /Ot /Gy /Ob3 /Gw /GL` | `cl.exe` | `target_compile_options` |
| `/LTCG /OPT:REF /OPT:ICF` | `link.exe` | `target_link_options` |

`/GL` and `/LTCG` are a pair — one without the other loses the optimisation — so link-time
optimisation is expressed as `CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE` instead of raw
flags, which keeps the two halves together and gives `-flto` on Clang for free.

## Tests

```bash
ctest --test-dir build/windows-x64 --build-config Debug --output-on-failure
```

The simulation takes a world snapshot and a delta time and produces sprite positions, and a
fake world is just a list of line segments — so the physics is tested with no compositor,
no windows and no platform involved at all. Occlusion clipping is tested the same way: it
is rectangles and a stacking order, and it is shared by both backends.

## Trying it

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

On Linux the geometry has to come from the compositor, so install the KWin script first:

```bash
./kwin/install.sh
```

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
out between them, so that is six dragons, two of each.

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

## Packages

Every build of `main` publishes a rolling **`nightly`** pre-release, and every CI run
attaches the same files as artifacts for fourteen days. Both come from the one set of
`install()` rules, so what is tested is what is shipped.

| File | What to do with it |
|---|---|
| `dragonperch_0.1.0~20260828.1830.g462431a_amd64.deb` | `sudo apt install ./dragonperch_*.deb` |
| `dragonperch_0.1.0~…_x86_64.tar.gz` | unpack anywhere and run `usr/bin/dragonperch-wl` |
| `dragonperch_0.1.0~…_x64.zip` | unpack and run `dragonperch.exe` |

Every nightly carries its build time and commit in the version, so `apt` upgrades one to
the next rather than refusing the newer file as a downgrade. The **tilde is what makes that
work**: Debian sorts `~` before everything, including nothing at all, so

```
0.1.0~20260828.1830.g462431a  <  0.1.0~20260829.0300.gdeadbee  <  0.1.0
```

— nightlies ascend, and a real `0.1.0` supersedes every nightly of it. `dragonperch
--version` prints exactly what is installed.

**The downloaded file's name will have a `.` where that `~` should be.** GitHub rewrites
anything outside `[A-Za-z0-9._-]` in a release asset's name, so `0.1.0~2026…` arrives as
`0.1.0.2026…`. It makes no difference: `dpkg` and `apt` read the version from the
package's control field, and the file name is a convention, not data. Check for yourself —

```bash
dpkg-deb -f dragonperch_*.deb Version
```

— and CI prints the same field on every run, alongside a `dpkg --compare-versions` that
fails the build if it ever stops sorting before the release version.

The artwork is found relative to the executable, so an unpacked tarball works without being
installed and without an environment variable. Installing the package does **not** enable
the KWin script and does **not** start anything at login — a program that puts dragons on
somebody's screen because a dependency pulled it in is a program that gets uninstalled.

### Windows Defender flags the download

`Trojan:Win32/Wacatac.B!ml` — the `!ml` is the tell: a machine-learning guess, not a
signature match. It is a false positive, and a predictable one. The binary is unsigned,
freshly built, downloaded by nobody yet, and what it does for a living is enumerate other
applications' windows, install a system-wide event hook, and keep a transparent
always-on-top window over the whole screen. That is also roughly what a screen-scraper does.

There is no trick that makes this go away honestly, and anything that did would be a trick
worth being suspicious of. The two real answers are:

- **report it** at <https://www.microsoft.com/en-us/wdsi/filesubmission> as a false
  positive. This works, and it is worth doing — it is how the file stops being flagged for
  everybody rather than just for you.
- **sign it.** A code-signing certificate is what gives a binary an identity and lets
  reputation accumulate against it. Until then every new build starts from zero, and
  SmartScreen will warn about it whether or not Defender does.

Build it yourself and the problem does not arise: a locally compiled binary is not a
download.

To build them yourself:

```bash
cmake --build --preset linux-x64-release && cd build/linux-x64 && cpack -C Release
```

## Layout

```
src/core/     portable. No OS headers — CI builds this alone to keep it that way.
src/win/      Windows head: DirectComposition + Direct2D + Win32.
src/linux/    Linux head: layer-shell + EGL/GL.            (milestone 6)
kwin/         KWin script. Runs inside the compositor, pushes geometry over D-Bus.
external/     upstream Wayland protocol XML, as submodules.
tools/        the sprite-pack generators. Inkscape and Pillow, run by hand, not by CMake.
packaging/    the .desktop file. Everything else about packaging is cmake/Packaging.cmake.
assets/       one directory per mascot. CC BY-SA 4.0, not GPL — see assets/README.md.
docs/plan.md  the plan of record, including findings that cost real time to establish.
```

## Licensing

Code is `GPL-3.0-or-later`.

Artwork is **not**. Konqi, Katie and Kori are the work of the KDE community — designed and
drawn by **Tyson Tan**, Konqi vectorised by **Franco Perez** — under `CC-BY-SA-4.0`. Keep
them that way; there is no reason to relicense data that is loaded at runtime rather than
linked. Each pack states its own terms in its `artwork-licence` key and credits its artists
in its `AUTHORS.md`.

[KDE's mascot material](https://community.kde.org/Promo/Material/Mascots) publishes Konqi as
an SVG and the other two as flat PNGs, which is why there are two generators in `tools/`.
Neither ships a walk cycle: the animation is made here out of a single pose. `assets/README.md`
says how.
