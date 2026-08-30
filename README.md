<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# DragonPerch

**Konqi and friends romp across your window title bars and panels — a cross-platform
XPenguins for Windows and Wayland.**

Pets walk along the top edge of your windows, sit on your taskbar, and fall off when you
drag the window out from under them. Konqi, Katie and Kori come with it; the artwork is a
sprite pack, so anybody's mascot can walk instead. C++23, GPU rendering, as close to the platform as
practical.

> Status: **Windows works.** Konqi, Katie and Kori walk on your title bars and taskbar,
> drawn on the GPU from KDE's own artwork; clicks pass through them, they get out of the way
> of full-screen apps, and there is a Fluent tray menu and settings window.
>
> **Linux works too.** The Wayland head draws through EGL on a layer-shell overlay, and a
> KWin script tells it where the windows are — verified on Plasma 6. There is a tray icon
> and a KDE settings module. What is next is in [the plan](docs/plan.md).

*[日本語版はこちら](docs/ja/README.md)*

## Getting it

Every build of `main` publishes a rolling **`nightly`** pre-release.

| You have | Do |
|---|---|
| Debian or Ubuntu | `sudo apt install ./dragonperch_*.deb` |
| Plasma, as well | `sudo apt install ./dragonperch-kde_*.deb` |
| Any other Linux | unpack the `.tar.gz` anywhere and run `usr/bin/dragonperch-wl` |
| Windows | unpack the `.zip` and run `dragonperch.exe` |

**Linux means Wayland, and today it means Plasma.** The overlay needs
`zwlr_layer_shell_v1`, which GNOME does not offer, and a Wayland client is not allowed to
know where your windows are — so something inside the compositor has to tell it, and so far
that is written for KWin and nothing else. On Sway or Hyprland the pets appear and have only
the bottom of the screen to walk on.

On Plasma there is **nothing to install or enable** for that: DragonPerch finds the KWin
script and asks the compositor to run it every time it starts, whether it came from the
package, the tarball or a build directory. [What you need](docs/requirements.md) has the
whole picture, and what to do on the day it goes wrong.

Windows Defender may flag the download. It is a false positive and
[there is a page about why](docs/packages.md#windows-defender-flags-the-download).

## Using it

Right-click the tray icon: **Pause**, **Settings**, **Quit**. That is the whole interface.

```bash
dragonperch --pets 6      # six of each mascot; dragonperch-wl on Linux
dragonperch --stop
```

[Running it](docs/running.md) has the rest of the command line and the diagnostic modes.
[Settings](docs/settings.md) has the configuration file and the two settings programs.

## Reading about it

| | |
|---|---|
| [What you need](docs/requirements.md) | Which systems and compositors work, and the KWin script |
| [How it is put together](docs/design.md) | The one constraint that decided the design, and the source layout |
| [Running it](docs/running.md) | Command line, tray icon, diagnostics, logs |
| [Settings](docs/settings.md) | The configuration file and the settings programs |
| [Packages](docs/packages.md) | What the downloads are, how versions sort, Defender |
| [Building from source](docs/building.md) | Presets, dependencies, compiler flags, tests |
| [Translating it](docs/translating.md) | One string table, and how to add a language |
| [The fuzz targets](fuzz/README.md) | What is fuzzed and what each target asserts |
| [The plan of record](docs/plan.md) | What is built, what is next, and findings that cost real time |

The pages people use rather than change are also in Japanese: [日本語](docs/ja/README.md).

## Licensing

Code is `GPL-3.0-or-later`.

Artwork is **not**. Konqi, Katie and Kori are the work of the KDE community — designed and
drawn by **Tyson Tan**, Konqi vectorised by **Franco Perez** — under `CC-BY-SA-4.0`. Keep
them that way; there is no reason to relicense data that is loaded at runtime rather than
linked. Each pack states its own terms in its `artwork-licence` key and credits its artists
in its `AUTHORS.md`.

[KDE's mascot material](https://community.kde.org/Promo/Material/Mascots) publishes Konqi as
an SVG and the other two as flat PNGs, which is why there are two generators in `tools/`.
Neither ships a walk cycle: the animation is made here out of a single pose.
[assets/README.md](assets/README.md) says how.
