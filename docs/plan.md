<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# DragonPerch — C++ implementation plan

Konqi and friends walk along window title bars and panels, on Windows and Linux.

This document is the plan of record. It exists mainly to carry forward things that were
established by measurement in an earlier C#/.NET prototype, so they do not have to be
rediscovered — several of them are counter-intuitive and cost real time to find.

---

## 1. Goals and constraints

| | |
|---|---|
| Language | C++23 (C++20 as the guaranteed floor for library features) |
| Rendering | GPU end-to-end on both platforms. No CPU blitting in the steady state. |
| Layer | As low as practical. Prefer the native API over a wrapper when the wrapper adds no capability we need. |
| Windows IDE | Visual Studio 2026, opened as a real `.sln` |
| Linux target | Plasma 6 on **Wayland**. wlroots compositors second; X11 and GNOME out of scope |
| Licence | Code `GPL-3.0-or-later`. Konqi artwork stays `CC-BY-SA-4.0`. |

Non-goals for v1: GNOME Shell extension, macOS, a plugin/scripting system.

---

## 2. What the prototype already established

These were measured, not reasoned. They apply regardless of language.

### The hard part is not drawing

Finding out where other applications' windows are is the difficult half, and on Wayland it
is deliberately impossible for a normal client. That single fact drives the architecture:
the simulation consumes a flat list of walkable line segments and never learns what an
HWND or a `wl_surface` is.

### Windows: the window styles that work

Click-through and GPU composition pull in opposite directions, and the resolution is
non-obvious. Measured with a harness that sent a real click via `SendInput` and checked
whether `WM_LBUTTONDOWN` arrived at a window underneath:

| Extended styles | Click reaches the window below |
|---|---|
| `WS_EX_LAYERED \| WS_EX_TRANSPARENT` | **yes** |
| `WS_EX_NOREDIRECTIONBITMAP \| WS_EX_TRANSPARENT` | **no** |
| `WS_EX_NOREDIRECTIONBITMAP \| WS_EX_LAYERED \| WS_EX_TRANSPARENT` | **yes** |

Consequences:

- `WS_EX_TRANSPARENT` alone does **not** pass clicks, whatever the window procedure
  answers to `WM_NCHITTEST`. Returning `HTTRANSPARENT` is not sufficient and testing it is
  not a click-through test.
- `WS_EX_LAYERED` and `WS_EX_NOREDIRECTIONBITMAP` are **not** alternatives. They coexist,
  and the combination is what makes a GPU-composited overlay click-through. The layered
  window must be given attributes once (`SetLayeredWindowAttributes(hwnd, 0, 255,
  LWA_ALPHA)`); its own bitmap is never used, because the content comes from the
  composition tree.
- `WS_DISABLED` **discards** clicks rather than passing them down. Never disable the
  overlay.

Composition content **does** render on a window carrying both flags — verified in milestone
1, on an Intel Iris Xe: an opaque quad appears, a half-transparent one over it blends
against both that quad and the desktop showing through the window, and the notification
state stays `QUNS_ACCEPTS_NOTIFICATIONS`. This was the last unknown in the Windows design
and it is now settled.

### Windows: Do Not Disturb is a size heuristic

`SHQueryUserNotificationState` returns `QUNS_BUSY` when a topmost borderless window covers
a monitor exactly, and Windows enables Do Not Disturb. One pixel of inset returns
`QUNS_ACCEPTS_NOTIFICATIONS`. This has nothing to do with composition, transparency or
topmost-ness — only size. Overlays must never exactly match a monitor rectangle.

### Windows: content islands are unusable here

`DesktopChildSiteBridge` + `ContentIsland` (the Windows App SDK hosting path, what WinUI 3
uses) creates a child HWND that **swallows mouse input across everything it covers** — with
a monitor-sized overlay, the entire desktop. Subclassing that child to answer
`HTTRANSPARENT` fixed hit testing and changed nothing about the clicks. Disabling it did
not help either. Content islands are an input-and-output island; we want output only.

This is why the plan below uses DirectComposition and not `Microsoft.UI.Composition`.

### Windows: window enumeration

- Filter on `IsWindowVisible`, `IsIconic`, `GetAncestor(GA_ROOT)`, `WS_EX_TOOLWINDOW`,
  `WS_CHILD`, and **`DWMWA_CLOAKED`**. Cloaked windows are invisible but report a
  plausible rectangle; without that check the desktop fills with ledges hanging in mid-air.
- Take geometry from **`DWMWA_EXTENDED_FRAME_BOUNDS`**, not `GetWindowRect`, which includes
  the invisible resize border (~7px) and puts pets visibly off the title bar.
- Occlusion clipping is not optional. Three maximised windows otherwise produce three
  identical full-width ledges at y=0, and since the overlay always draws on top, a pet on a
  buried ledge appears to float over the window covering it.
- Never poll. `SetWinEventHook` with `WINEVENT_OUTOFCONTEXT` over
  `EVENT_SYSTEM_FOREGROUND..EVENT_SYSTEM_MINIMIZEEND` and
  `EVENT_OBJECT_SHOW..EVENT_OBJECT_LOCATIONCHANGE`, on a thread with a real message pump.
  The hook callback must do no work beyond raising a flag: it runs on the delivery queue,
  and stalling it stalls the window being dragged.

### General

- A verification that does not exercise the real mechanism is worse than none. Two separate
  faults in the prototype were "confirmed fixed" by checks that measured the wrong thing.
  Every milestone below therefore names how it is measured.
- **Include what you use.** MSVC pulls in far more transitively than Clang does, so a
  missing `<cmath>` or `<limits>` builds cleanly on Windows and fails on Linux. This is not
  hypothetical: milestone 2 shipped six of them, and the Linux CI job was what caught it.
  There is no local guard for this on Windows — that job is the guard.

---

## 3. Build system

**CMake is the single source of truth; the Visual Studio solution is generated from it.**

```bash
cmake --preset windows-x64      # -> build/windows-x64/DragonPerch.sln
cmake --preset linux-x64        # -> build/linux-x64, Ninja
```

`CMakePresets.json` holds both. The Windows preset uses the `Visual Studio 18 2026`
generator, which produces a genuine `.sln` that opens, builds, and debugs exactly as
before.

The one trade-off worth stating up front: project properties changed inside Visual Studio
do **not** persist, because CMake regenerates the projects. Build settings belong in
`CMakeLists.txt`. In exchange there is one build description rather than two that drift.

(VS 2026 can also open the folder directly via `CMakePresets.json`, with no `.sln` at all.
Both work; the generated solution is the default here because it matches the existing
workflow.)

### Toolchain

| | Windows | Linux |
|---|---|---|
| Compiler | MSVC 19.51 (VS 2026) | Clang 22, named explicitly |
| Generator | Visual Studio 18 2026 | Ninja Multi-Config |
| Standard | C++23 | C++23 |
| CI image | `windows-2025-vs2026` | `ubuntu-26.04` |

CI installs nothing on either platform. The Windows image carries Visual Studio 2026 and
the generator configures MSVC itself; the Linux image carries Clang 22. The preset names
`clang-22` rather than `clang`, since plain `clang` on Ubuntu 26.04 is 21.

One compiler per platform rather than several. Building with both Clang and GCC would be a
better portability check, and this trades that away for a simpler pipeline.

All compiler and linker flags live in `cmake/CompilerOptions.cmake`, on a single interface
target. Release-only flags use `$<$<CONFIG:Release>:...>`, because the multi-config
generators choose the configuration at build time and a `CMAKE_BUILD_TYPE` test at configure
time silently does nothing.

Warnings as errors on both (`/W4 /WX`, `-Wall -Wextra -Wpedantic -Werror`), matching the
prototype's policy, which caught real problems.

### Dependencies

Deliberately few. Almost everything needed is a platform SDK.

| Dependency | Where | Source |
|---|---|---|
| `d3d11`, `dcomp`, `d2d1`, `dxgi`, `dwmapi`, `shell32`, `user32` | Windows | Windows SDK |
| `wayland-client`, `wayland-egl`, `wayland-protocols`, `wayland-scanner` | Linux | pkg-config |
| `egl`, `glesv2` | Linux | pkg-config |
| `wlr-layer-shell-unstable-v1.xml`, `xdg-shell.xml` | Linux | **submodules** under `external/`, fed to `wayland-scanner` |
| sd-bus (`libsystemd`) | Linux | pkg-config |
| Catch2 | tests | vcpkg manifest |

Vendor the layer-shell XML rather than fetching it. It is MIT and small, wlr-protocols cuts
no releases so there is no revision to pin, and a build-time download broke CI in the
prototype.

vcpkg is bundled with VS 2026 (`VC\vcpkg`); manifest mode via `vcpkg.json` keeps it
reproducible on both platforms.

---

## 4. Repository layout

```
DragonPerch/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ vcpkg.json
├─ src/
│  ├─ core/                  static lib. No OS headers, no platform types.
│  │   ├─ geometry.hpp         PixelPoint / PixelRect, global physical pixels
│  │   ├─ world.hpp            WalkableEdge, OutputInfo, WorldSnapshot, IWorldProvider
│  │   ├─ render.hpp           SpriteDraw, ISpriteRenderer, IFrameClock
│  │   ├─ sprites.{hpp,cpp}    Animation, SpritePack, atlas loading
│  │   ├─ simulation.{hpp,cpp} walk / fall / turn / idle state machine
│  │   └─ host.{hpp,cpp}       the loop that wires the three interfaces together
│  ├─ win/                   Windows head
│  │   ├─ overlay_window.*     the HWND and its styles
│  │   ├─ dcomp_renderer.*     DirectComposition + Direct2D
│  │   ├─ desktop_scanner.*    EnumWindows, DWM, occlusion clipping
│  │   ├─ win_event_watcher.*  SetWinEventHook + pump thread
│  │   └─ selftest.*           click-through and notification-state harness
│  └─ linux/                 Linux head
│      ├─ layer_surface.*      zwlr_layer_shell_v1 + EGL
│      ├─ gl_renderer.*        sprite batcher
│      ├─ kwin_world.*         D-Bus receiver for the KWin script
├─ external/                 upstream Wayland protocol XML, as submodules
├─ kwin/dragonperch-geometry/ KWin script (JavaScript, runs inside the compositor)
├─ assets/konqi/             CC BY-SA 4.0 artwork
├─ tests/                    Catch2, core only
└─ docs/
```

The rule that made the prototype portable, restated: **if a header under `src/core/`
includes a platform header, the port has gone wrong.** A CI job builds `core` alone to
enforce it.

---

## 5. Core abstractions

Carried over unchanged; they held up.

```cpp
enum class EdgeKind { WindowTop, PanelTop, ScreenFloor, ScreenCeiling };

struct WalkableEdge {
    std::int64_t owner_id;   // backend-stable identity, so a pet rides a dragged window
    int y, left, right;
    EdgeKind kind;
    int z_order;
};

class IWorldProvider {           // event driven, never polling
    virtual const WorldSnapshot& current() const = 0;
    virtual void on_changed(std::function<void(const WorldSnapshot&)>) = 0;
};

class ISpriteRenderer {          // alpha-blended axis-aligned blits, nothing more
    virtual int  register_atlas(std::span<const std::byte>, PixelSize) = 0;
    virtual void begin_frame() = 0;
    virtual void draw(const SpriteDraw&) = 0;
    virtual void end_frame() = 0;
};

class IFrameClock {              // driven by the compositor, never by a timer
    virtual std::chrono::nanoseconds wait_for_next_frame() = 0;
};
```

Coordinates are **global physical pixels**, origin at the top-left of the virtual desktop.
Scale conversion happens only inside a backend, at the last moment — on Wayland
`set_margin` takes logical coordinates, on Windows the process is per-monitor DPI aware.

---

## 6. Windows head

### Window

```
CreateWindowExW(
    WS_EX_NOREDIRECTIONBITMAP    // content comes from the composition tree
  | WS_EX_LAYERED                // makes WS_EX_TRANSPARENT actually pass clicks
  | WS_EX_TRANSPARENT
  | WS_EX_TOOLWINDOW             // out of Alt-Tab and the taskbar
  | WS_EX_NOACTIVATE             // never steals focus
  | WS_EX_TOPMOST,
    ..., WS_POPUP, ...);
SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
```

Sized strictly smaller than its monitor. One overlay per monitor, inset by one pixel, is
the starting point; per-pet windows remain an option if anything else turns up.

### Rendering

```
D3D11CreateDevice(..., D3D11_CREATE_DEVICE_BGRA_SUPPORT, ...)
  -> IDXGIDevice
DCompositionCreateDevice3(dxgiDevice, IID_PPV_ARGS(&dcompDevice))   // IDCompositionDesktopDevice
  -> CreateTargetForHwnd(hwnd, TRUE, &target)
  -> CreateVisual(&visual);  CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM,
                                           DXGI_ALPHA_MODE_PREMULTIPLIED, &surface)
  -> visual->SetContent(surface);  target->SetRoot(visual)

per frame:
  surface->BeginDraw(&dirtyRect, IID_PPV_ARGS(&dxgiSurface), &offset)
  d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface, ..., &bitmap)
  d2dContext->SetTarget(bitmap);  BeginDraw();  Clear(transparent);  DrawBitmap(atlas, ...)
  EndDraw();  surface->EndDraw();  dcompDevice->Commit()
```

`IDCompositionSurface::BeginDraw` takes a dirty rectangle, so damage tracking is native
here — no readback, no CPU copy, the atlas stays a GPU texture.

Direct2D is used directly rather than through Win2D. Win2D is a WinRT wrapper over Direct2D
whose value is its effects library and its `CanvasComposition` bridge to
`Microsoft.UI.Composition`; neither applies once composition is DirectComposition and the
drawing is axis-aligned bitmap copies. From C++ the wrapper costs more than it saves.

### Frame pacing

`DwmFlush()` on the loop thread. It blocks until DWM finishes its next present, which
self-throttles to the refresh rate — the Windows counterpart to `wl_surface.frame`. Pump
messages before each flush; the overlay must stay responsive to `WM_NCHITTEST`.

### Geometry

As in §2. `SetWinEventHook` on a dedicated pump thread, flag-only callback, coalesced to
one scan per frame at most.

---

## 7. Linux head

### Wayland (Plasma, wlroots)

```
wl_display_connect -> registry -> bind wl_compositor, zwlr_layer_shell_v1, wl_output[],
                                       wp_fractional_scale_manager_v1?
zwlr_layer_shell_v1_get_layer_surface(surface, output, LAYER_OVERLAY, "dragonperch")
  set_anchor(TOP|BOTTOM|LEFT|RIGHT); set_exclusive_zone(-1); set_keyboard_interactivity(NONE)
wl_surface_set_input_region(surface, <empty>)        // click-through
commit -> await configure -> ack_configure
wl_egl_window_create + eglCreateWindowSurface        // EGL_ALPHA_SIZE 8, or it is opaque black
```

Drawing is OpenGL ES: one atlas texture, one shader, one instanced quad draw per frame.
`eglSwapBuffers` submits through `zwp_linux_dmabuf_v1` underneath, so the buffer is never
copied through the CPU. Pacing is `wl_surface.frame` — the only correct clock, and it stops
delivering when the surface is occluded, which pauses the pets for free.

In C++ this needs **no shim**: `wayland-scanner` output compiles directly, and EGL/GL are C
APIs. The `libdragonperch_wl.so` layer the C# design required disappears entirely.

### Geometry

A Wayland client cannot see other clients' windows. The compositor has to tell us:

- **Plasma**: a KWin script running inside KWin pushes window rectangles over D-Bus. It
  is the only side that can see other clients' windows. The script
  is already written (JavaScript, `kwin/dragonperch-geometry/`) and carries over unchanged.
  Client side: sd-bus, with all coalescing and rate limiting on our side — a KWin script
  runs on the compositor's main thread and anything slow there is session-wide jank.
- **wlroots**: `swaymsg -t get_tree` / `hyprctl clients -j` adapters.
- **X11**: not supported. See §13.1.

---

## 8. Settings

Out of scope for v1, but the shape is decided now because it removes a constraint:

**The settings UI is a separate program, not part of the daemon.** They communicate through
a config file only. This lets each platform use its native toolkit without forcing the
daemon's language.

- Windows: WinUI 3 + Windows Community Toolkit (`SettingsCard`), which is Fluent by
  construction. Easiest in C#, and that is fine — it is a different executable. The Windows
  App SDK dependency lives there, not in the daemon.
- Linux: Kirigami + KConfig, ideally packaged as a KCM so it appears in System Settings.
  Qt/QML, so C++ is natural.

Config format: INI, at `~/.config/dragonperch/dragonperchrc` and
`%APPDATA%\DragonPerch\config.ini`. INI keeps the Linux side compatible with KConfig
conventions.

---

## 9. Testing

- **Core** — Catch2. The simulation takes a `WorldSnapshot` and a delta time and produces
  sprite positions; a fake world is just a list of line segments, so physics is testable
  with no compositor anywhere.
- **`--self-test` on Windows** — carried over, and it earned its place. It creates a target
  window on its own thread under the overlay, sends a real click with `SendInput`, and
  reports whether `WM_LBUTTONDOWN` arrived; it also reports
  `SHQueryUserNotificationState`. Both regressions it catches were shipped once already.
- **`--dump-world`** — prints the walkable edges with the class and caption of the window
  behind each, so bogus ledges can be identified by eye.
- **CI** — GitHub Actions: `core` alone (portability guard), Windows MSVC, Linux GCC.

---

## 10. Milestones

Each names how it is verified. No milestone is done because it compiles.

| # | Deliverable | Verified by |
|---|---|---|
| 0 | ~~CMake skeleton, presets, CI, `core` building on both platforms~~ **done** | `DragonPerch.slnx` generated and builds; Debug and Release both exit 0 |
| 1 | ~~**Composition renders on a layered window**~~ **done** | Screenshot shows opaque and blended quads over a visible desktop |
| 2 | ~~Core simulation ported, Catch2 tests~~ **done** | 20 tests pass on both configurations; a fake world lands a pet exactly on the edge's row |
| 3 | ~~`desktop_scanner` + `win_event_watcher`~~ **done** | `--dump-world` tracks a scripted window 12px per step; occlusion clips a maximised window to its visible run |
| 4 | ~~Windows head end to end, placeholder sprites~~ **done** | Dragons render on the taskbar through DirectComposition; `--self-test` PASSes with a sprite over the click point; notification state stays `QUNS_ACCEPTS_NOTIFICATIONS` |
| 5 | ~~Fullscreen detection~~ **done** | A borderless full-screen window hides the pets on that monitor and closing it brings them back, in exactly two transitions |
| 6 | ~~Wayland layer-shell surface + EGL on Plasma~~ **done** | Dragons visible on Plasma Wayland under llvmpipe; clicks pass through; `--probe-composition` tints the screen and reports frames presented |
| 7 | ~~KWin script + sd-bus geometry~~ **done** | Pets stand on real Plasma title bars and on the panel; `--dump-world` prints KWin's report as sent |
| 8 | ~~KDE mascot artwork replaces the placeholder~~ **done** | Konqi, Katie and Kori walk on the taskbar together, each facing the way it is going; the two that carry KDE's K draw both directions rather than mirroring |
| 9 | Tray icon, on both platforms | Right-click gives pause, settings and quit; no console needed to stop it |
| 10 | Settings, on both platforms | Changing the pet count takes effect without a restart |
| 11 | Pause for full-screen apps on Wayland | A full-screen window hides the pets on that monitor, as it already does on Windows |
| 12 | wlroots adapters | Pets on title bars under Sway and Hyprland |

Milestones 6 and 7 took four rounds on a virtual machine after CI was green, which is the
whole argument for this column. Every one of the four was invisible to a compiler: an
unmapped surface that made the render loop wait for a frame callback that could never come;
`signal()` installing a handler with `SA_RESTART` so Ctrl+C never woke the loop; a KWin
script with no way to introduce itself to a client that started after it; and two separate
faults that each filtered every ordinary window while leaving panels working.

Milestone 1 is deliberately tiny and first, because it is the one remaining unknown in the
Windows design. If composition content will not render on a layered window, the whole
Windows plan changes, and that is worth finding out on day one rather than after the
simulation is ported.

---

## 11. Open risks

1. ~~Layered + composition rendering.~~ Settled in milestone 1: it works. Milestone 4 added
   one more rule to it: **a DirectComposition surface rejects a partial `BeginDraw` until
   the whole surface has been written once.** The rectangle it refuses looks perfectly
   valid, and the error is a bare `E_INVALIDARG`, so nothing about it points at the cause.
   The same failure went unexplained in the C# prototype, where damage tracking was
   abandoned because of it.
2. **Fractional scaling on Wayland.** `set_margin` is logical, everything above is physical.
   One conversion point, easy to get subtly wrong, worth a test.
3. **KWin scripting API drift.** Plasma 5 and 6 renamed `clientList`/`windowList`. The
   existing script probes rather than assumes; keep it that way.
4. **Multi-monitor with mixed DPI on Windows.** Per-monitor-v2 in the manifest is mandatory;
   without it window rectangles are reported in the wrong space on the secondary monitor.

---

## 12. What carries over from the prototype

| Artefact | Status |
|---|---|
| KWin script (`kwin/dragonperch-geometry/`) | Reusable as-is — it is JavaScript running inside KWin |
| Core simulation design | Port directly; the shape was validated |
| `--self-test` and `--dump-world` | Port; both found real bugs |
| Placeholder sprite generator | Port — it made the renderer verifiable without artwork |
| Window-style and notification-state findings | This document, §2 |
| C#-specific interop layers | Discarded; C++ does not need them |

---

## 13. What comes next: a tray icon and settings

Three separate pieces of work. They are written down together because the first one decides
a refactor the other two live with.

### 13.1 X11: decided against

An X11 backend was planned and is **not going to be built**. Recording why, so that it is
clear this was weighed rather than forgotten.

It would not have been a second renderer -- `GlesRenderer` draws through EGL, and EGL runs
on X11 as happily as on Wayland, so only the native surface differs. The overlay is an
override-redirect ARGB window made click-through with an empty `XShape` input region, which
is about as short as the Wayland equivalent. That part was cheap.

The expensive part is everything around it. A second `IWorldProvider` over EWMH, with its
own quirks per window manager. A second frame clock, and a worse one: X11 has no
`wl_surface.frame`, so the "an overlay nobody is looking at costs nothing" property is
simply lost. A second set of multi-monitor and scaling rules. And every future feature --
the tray, full-screen detection, settings -- paying for a third platform for ever.

Against that: Plasma 6 defaults to Wayland and its X11 session is on the way out, which is
the direction the one target that matters here is already moving. A KWin/X11 session would
also still need the *overlay* half, so the existing script buys nothing on its own.

Two backends is the shape this design was drawn for. A third would be the first thing to
make the core/backend split cost more than it returns.

### 13.2 Tray icon (milestone 9)

This is what makes `--stop` stop being the answer, and it is the piece that most changes
how the program feels to live with.

**Windows** is straightforward and belongs in the daemon: `Shell_NotifyIcon` on a hidden
message-only window, a `TrackPopupMenu` with Pause, Settings and Quit. A hundred lines of
Win32 and no dependency. This is also where being a GUI-subsystem binary finally pays for
itself.

**Linux has no equivalent that is merely an API call.** The XEmbed system tray does not
exist on Wayland. The mechanism Plasma actually uses is **StatusNotifierItem**: register
`org.kde.StatusNotifierItem-<pid>-1` on the session bus, hand it to
`org.kde.StatusNotifierWatcher`, and expose a menu through `com.canonical.dbusmenu`. All of
it is D-Bus, so sd-bus can do it with no toolkit — but dbusmenu in particular is a fiddly
interface, and this is several hundred lines rather than a hundred.

The alternative is to let the tray live in the settings program, where a toolkit already
is, and have it drive the daemon over D-Bus. That keeps the daemon toolkit-free at the cost
of a second process that must always be running.

**Recommendation: hand-write StatusNotifierItem in the daemon.** It matches what the
Windows side does, keeps "one process, no toolkit" true on both platforms, and the daemon
already owns a bus name and runs an sd-bus loop. If no watcher is registered — a bare
wlroots session with no tray — log it and carry on; `--stop` and the console handler remain.

### 13.3 Settings (milestone 10)

§8 already decided the shape and it still looks right: **the settings UI is a separate
program**, so each platform gets its native toolkit without imposing it on the daemon.
WinUI 3 and the Windows Community Toolkit on Windows, Kirigami and KConfig on Linux, ideally
as a KCM so it turns up in System Settings.

What §8 did not settle is how a change reaches a running daemon. A config file alone means
either polling or a file watcher, and the daemon would have to guess when a half-written
file is finished.

**Add a control interface, and reuse what is already there.** The daemon owns
`org.dragonperch.Geometry` on Linux; give it `org.dragonperch.Control` alongside, with
`Reload()`, `SetPaused(b)` and `Quit()`. On Windows the same three over a named pipe — the
`--stop` event becomes one case of it. The settings program writes the file and calls
`Reload()`; the tray menu calls `SetPaused` and `Quit`. One mechanism, three callers.

Settings worth having, in the order they are worth having:

| Setting | Why |
|---|---|
| How many pets, and which mascots | The first thing anyone wants to change |
| Walk speed, idle frequency | `SimulationOptions` already carries these |
| Which monitors to use | A pet wandering onto a TV is not wanted |
| Start with the session | Currently a manual copy into `~/.config/autostart` |
| Pause while a full-screen app is running | Implemented on Windows, not yet on Linux |

The last row is a gap worth naming: `fullscreen.cpp` exists on the Windows head and has no
Wayland counterpart, because a Wayland client cannot see that another window is full-screen.
The KWin script can, and the report format has a version number precisely so a field can be
added to it.
