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

### Windows: the first draw has to clear all of what it claims

`IDCompositionSurface::BeginDraw` rejects a partial update while any of the surface is
still undefined, so the first draw widens its update rectangle to the whole surface. The
clear inside it has to widen too. Clipping the clear to the *requested* dirty rectangle
while telling DirectComposition the update covers everything leaves the rest of the surface
never written at all.

In a release build that leftover is zeroes, so it is transparent and the bug is invisible.
With `D3D11_CREATE_DEVICE_DEBUG` the debug layer fills new resources with a pattern, and it
showed up as **97.6% of the screen tinted green** -- fading wherever a dragon had walked,
because that is where damage finally covered it. Measured before and after: 97.6% of the
screen down to 0.05%, which is one falling dragon.

Worth knowing because the symptom points away from the cause twice over: it looks like a
debug-only artefact, and it looks like something drawing green rather than something not
drawing at all.

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

It is a finding about **overlays**, and not an argument against WinUI anywhere else: an
island swallowing input is fatal to a click-through surface and irrelevant to a menu, which
wants input captured. See §13.3.

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

### What is actually in the binary

Measured on the Windows head, Release, diagnostics off, from a linker map:

| | bytes | share |
|---|---:|---:|
| `std::format` — Ryu float tables and Unicode property data | ~130,000 | ~32% |
| our own code, the CRT, and the import tables | ~272,000 | |
| **total** | **401,920** | |

**`std::format` costs about a third of the binary, and does so whatever you format.** A
program whose only formatting is `std::format("{} and {}", an_int, "text")` links
`xcharconv_ryu_tables.obj` and comes to 215,552 bytes; the same program written with
`std::to_string` and `+` is 14,336. MSVC's `std::format` type-erases its arguments and the
visitor instantiates the floating-point path regardless, so the tables come whether a float
is ever passed or not.

It was replaced, and the binary halved: **390,144 bytes to 196,096**. Integer conversion is
free, which is what makes that possible --

| | bytes | Ryu tables linked |
|---|---:|---|
| `std::format`, integers only | 215,552 | yes |
| `std::to_string` | 14,336 | no |
| `std::to_chars` on an integer | 10,240 | no |
| `std::to_chars` on a double | 132,608 | yes |

-- so `dragonperch/text.hpp` joins strings and whole numbers with `std::to_chars`, and
deliberately cannot be passed a `double`. Nothing this program prints needs one. Diagnostic
code keeps `std::format`, where the padding and alignment earn their place and the size is
not paid, because it is compiled out of a release build.

For scale, the two next-largest savings: the diagnostic modes were 25,088 bytes (5.9%) and
the procedural placeholder 11,776 (2.9%). Together they are a quarter of what `std::format`
alone cost.

With that gone the map is flat -- nothing left is more than a fifth of the binary -- and
what remains is mostly the standard library doing ordinary work:

| | bytes | note |
|---|---:|---|
| `pack_library.obj` | ~38,000 | `std::filesystem`: `directory_iterator` and `path` |
| `sprite_pack_file.obj` | ~24,000 | the INI parser, `std::map`, `std::vector<std::pair<std::string, ...>>` |
| `msvcprt:vector_algorithms.obj` | ~15,000 | MSVC's SIMD tables for `std::find` and friends |

The two that could still go are `<filesystem>`, if directory enumeration moved into the
heads behind a callback the way image decoding already has, and the `std::map` in a
`SpritePack`, which could be a sorted vector. Both are worth perhaps twenty kilobytes
between them and neither is free: the first adds a platform-specific path to each head, and
the second trades a clear container for a hand-rolled lookup. Neither is worth doing until
something else makes it worth doing.

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
`%APPDATA%\DragonPerch\dragonperchrc`. INI keeps the Linux side compatible with KConfig
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
| 9 | ~~Control interface, then a tray icon on both platforms~~ **Windows done, Linux written** | Right-click gives pause, settings and quit, in Breeze on Linux and WinUI on Windows; `--stop` becomes one caller of the same mechanism; the icon survives an Explorer restart and a tray appearing late; the daemon still builds and runs with no shell installed |
| 10 | Settings file, then a settings page in each shell | Changing the pet count takes effect without a restart; the daemon still builds with no Qt, no .NET and no App SDK |
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

### Known limitations

Named because they are decisions, not oversights.

- **A monitor's scale is read once.** `LayerSurface` sets `wl_surface.set_buffer_scale`
  from the scale the output had when it was created. Changing a display's scaling while
  DragonPerch is running leaves the pets drawn at the old size until it is restarted.
  Following it means listening for `wl_output.scale` -- or `wl_surface.preferred_buffer_scale`
  on a new enough compositor -- and rebuilding the EGL window, which is the surface code
  this program has got wrong more often than any other. Not worth doing badly.
- **A paused loop does not dispatch Wayland.** `PetHost` sleeps rather than waiting on the
  frame clock while paused, so a compositor that went away during a pause is not noticed
  until it resumes. Stopping uses the signal flag and the control interface, and both work.

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

### 13.2 The control interface (milestone 9, first half)

Both remaining milestones need the same thing before either can start: a way to tell a
running DragonPerch something. The tray needs *quit* and *pause*; settings needs *reload*.
Building one mechanism for all three is the point of doing this first.

| | Transport | Endpoint |
|---|---|---|
| Linux | D-Bus, on the connection already open | `org.dragonperch`, object `/org/dragonperch/Control` |
| Windows | `WM_COPYDATA` to a message-only window | class `DragonPerch.Control`, found with `FindWindowExW` |

One well-known name for the program, with an object per thing it does -- geometry reports
arrive at `/org/dragonperch/Geometry` on the same name. Two names for one process was an
accident of the geometry object having been written first, and the KWin script ships in the
same package as the binary, so there was never a version to be skewed against.

```
Quit()                  stop and unwind
SetPaused(b paused)     freeze the simulation, keep the surfaces
Paused() -> b
Reload()                re-read the settings file
```

Neither transport needs a new thread, which is why these two and not something else. On
Linux `KWinGeometryProvider` already runs an sd-bus loop and a second vtable on the same
connection costs nothing. On Windows the tray needs a message-only window anyway, and the
overlay windows are already pumped on the main thread, so the same window answers both --
`WM_COPYDATA` is the one Win32 IPC that arrives as a message rather than needing a server.

`--stop` becomes `Quit()` and the named event goes away. Two mechanisms for one job was
tolerable while there was one job.

*Pause* is worth stating precisely: the simulation stops advancing and the overlays stay
mapped, showing the last frame. Tearing the surfaces down and rebuilding them would be a
second code path through everything that has already been got wrong once.

### 13.3 Tray icon (milestone 9, second half)

**No toolkit in the daemon, on either platform.** That is the rule; where the tray itself
lives follows from it rather than the other way round.

On Linux the tray needs no toolkit at all, so all of it goes in the daemon. On Windows the
icon is equally cheap and stays there too, but a menu that matches where the system is
heading does need a toolkit -- so the menu alone is handed to `DragonPerch.Shell.exe`,
started only when somebody opens it. That program also carries the settings window §13.4
was already going to write in the same technology. See "who draws the menu" and "paying for
WinUI" below.

#### Windows: `Shell_NotifyIcon`, in the daemon

Roughly two hundred lines of C++ and no dependency. The icon itself is cheap and has to be
there whenever the pets are, so it stays with them; only the *menu* is delegated to a
process that can afford a toolkit. See "paying for WinUI" below.

```
message-only window (HWND_MESSAGE), class DragonPerch.Control
Shell_NotifyIconW(NIM_ADD)   NIF_ICON | NIF_TIP | NIF_MESSAGE, NOTIFYICON_VERSION_4
WM_APP+1                     the callback message; WM_CONTEXTMENU and NIN_SELECT
CreatePopupMenu + TrackPopupMenuEx(TPM_RIGHTBUTTON | TPM_RETURNCMD)
```

Two details that are always forgotten and are the whole reason to write them down:

- **`TaskbarCreated`.** Explorer restarts more often than people think, and takes every
  tray icon with it. Register the message with `RegisterWindowMessageW(L"TaskbarCreated")`
  and re-add the icon when it arrives, or DragonPerch quietly loses its only user
  interface until the next login.
- **`SetForegroundWindow` before `TrackPopupMenuEx`**, and a posted null message after.
  Without it the menu does not dismiss when clicked away from, which looks like a hang.

The `.ico` wants 16, 20, 24, 32, 48 and 256, generated from `Konqi.svg` by the same
Inkscape and Pillow the sprite packs use, and `LoadIconWithScaleDown` picks between them.

#### Linux: StatusNotifierItem by hand

There is no equivalent of one shell call. XEmbed system trays do not exist on Wayland, and
what Plasma implements is **StatusNotifierItem** -- D-Bus, so sd-bus can do it, but four
interfaces rather than one function.

```
own  org.kde.StatusNotifierItem-<pid>-1
export /StatusNotifierItem   org.kde.StatusNotifierItem
    properties  Category=ApplicationStatus, Id, Title, Status=Active,
                IconPixmap a(iiay), IconName, ToolTip, Menu=/MenuBar, ItemIsMenu=true
    methods     Activate(ii), SecondaryActivate(ii), ContextMenu(ii), Scroll(is)
    signals     NewIcon, NewStatus, NewToolTip
export /MenuBar              com.canonical.dbusmenu
    GetLayout(i i as) -> (u (ia{sv}av)),  Event(i s v u),  AboutToShow(i) -> b
    signals     LayoutUpdated, ItemsPropertiesUpdated
call org.kde.StatusNotifierWatcher.RegisterStatusNotifierItem(s)
```

`GetLayout` returning `(ia{sv}av)` recursively is the fiddly part -- a variant holding an
array of variants holding structs -- but the menu here is four flat items, so the recursion
is one level and `sd_bus_message_open_container` handles it in about a hundred and fifty
lines. Estimate for the whole thing: five to six hundred lines, which is the honest price
of not linking Qt.

And it is bought back immediately, because those hundred and fifty lines describe a menu
rather than draw one -- see below.

**Use `IconPixmap`, not just `IconName`.** A themed icon name needs the icon installed into
`hicolor`, and the tarball is meant to run unpacked. The daemon already decodes PNG with
libpng, so ship a 64×64 PNG beside the artwork and marshal it as ARGB32 in network byte
order -- twenty lines, and it works whether or not anything was installed. Set `IconName`
as well, so a desktop that prefers themed icons gets one.

`StatusNotifierWatcher` is the counterpart of `TaskbarCreated`: watch `NameOwnerChanged`
for it appearing and re-register. If it never appears -- a bare wlroots session with no
tray -- log it once and carry on. Ctrl+C still works, and so does `Quit()`.

#### What the menu looks like, and who draws it

The two platforms are not symmetric here, and it is worth being plain about which way each
one cuts.

**On Linux the menu is not ours to draw.** dbusmenu is a description, not a rendering:
DragonPerch sends labels, separators, toggle states and icon names, and *Plasma's own tray
widget builds the menu from them*. So it is Breeze, in Qt 6, with the user's colour scheme,
font, icon theme and scale factor — not because we matched them but because we never had a
say. Theme changes follow with no code at all. This is the protocol working as intended,
and it is the strongest argument for having chosen it: writing the menu ourselves could
only be worse.

**On Windows the menu is ours, and it is going to be a real WinUI 3 `MenuFlyout`.**

An earlier draft of this section recommended a `TrackPopupMenuEx` system menu, on the
grounds that a tray context menu is one of the places where a system menu *is* the native
answer. That was true and is becoming less true every release: Windows 11 has been moving
its own surfaces onto WinUI, the Run dialog included, and a design that is merely
system-consistent today is inconsistent with where the system is going. An imitation drawn
with Direct2D would have the same problem twice over — it would drift as Fluent moves, and
it would be our job to keep chasing it.

Two of the three objections in that draft do not survive contact:

- *"Content islands swallow mouse input"* — measured in milestone 1, and irrelevant here.
  That was a click-through overlay, where input passing through was the whole requirement.
  A menu wants input captured. If anything it is evidence the mechanism captures reliably.
- *"It imitates Fluent, so it drifts"* — that was the argument against owner drawing, not
  against using the real thing.

The objection that does survive is weight, and it decides where the code goes rather than
whether to write it. WinUI 3 means the Windows App SDK: a runtime the user must have, or
forty-odd megabytes of it shipped alongside, plus XAML initialisation in startup time and
tens of megabytes of working set. In a settings window nobody would notice. In a process
whose entire claim is that a desktop pet costs nothing to leave running all day, it would
make the proudest property of this program untrue.

**So the Fluent UI goes in a process of its own, and the daemon does not change.**

```
dragonperch.exe          Win32, D2D, DirectComposition. No toolkit, no App SDK.
                         Runs on its own; needs nothing below it.

DragonPerch.Shell.exe    C#, WinUI 3, Windows App SDK.
                         Draws the Fluent MenuFlyout, on demand.
                         Hosts the settings window (§13.4).
                         Drives the daemon over WM_COPYDATA (§13.2).
```

This is not a new technology: §13.4 already put the settings window in C# and WinUI 3. It
merges two planned things into one program rather than adding a third. The flyout is shown
from a transparent host window positioned at the cursor — about a hundred and fifty lines.
`H.NotifyIcon.WinUI` packages that if the hand-rolled version proves tedious; a NuGet
dependency in a replaceable UI program is a very different proposition from one in the
daemon.

The dependency runs one way only. The shell can quit the daemon; the daemon neither knows
nor cares whether a shell is installed, so `--pets 6` from a terminal, an autostart entry,
and a headless test all work with nothing else present.

The structural asymmetry with Linux is deliberate and invisible: on both platforms the menu
is drawn by whatever the platform considers modern, and on both the daemon stays a small
Win32-or-sd-bus background process. Linux gets there for nothing because dbusmenu is a
description; Windows gets there by putting the toolkit somewhere it can afford to be.

#### Paying for WinUI only when somebody looks at the menu

The obvious objection to the split above is that it pays for XAML all day so that a menu can
be right four times a year. Worth taking seriously: most people will leave DragonPerch
running for weeks and never once open its menu.

Loading late and unloading after are not the same problem, though.

**Loading late works.** Neither half of the Windows App SDK has to be initialised at
startup: `MddBootstrapInitialize` can be called the first time it is wanted, and
`WindowsXamlManager::InitializeForCurrentThread` after it. Deferring both until a menu is
actually asked for costs nothing and is worth doing whatever else is decided -- a pet that
appears at login before the shell has finished thinking about XAML is strictly better than
one that does not.

**Unloading is the doubtful half** -- and the experiment has now been run, on .NET 10 with
Windows App SDK 2.4.0, unpackaged and self-contained. It asked two questions and the second
one turned out to be the one that mattered.

*Can XAML be shut down and started again in the same process?* **Yes**, repeatably. Eight
rounds of `DispatcherQueueController.CreateOnCurrentThread` ->
`WindowsXamlManager.InitializeForCurrentThread` -> `Close` -> `ShutdownQueueAsync` all
succeeded. This contradicts what this section previously assumed, and the assumption was
worth checking rather than designing around.

One trap, because it produced a confident wrong answer first: `ShutdownQueueAsync` does its
work *on the thread's own dispatcher queue*, so a version of the experiment that sleeps
instead of pumping messages never actually tears the queue down. Round two then fails with
`DispatcherQueueController is already created on this thread`, which reads exactly like the
unsupported-path answer it was looking for. Run a real `GetMessage`/`DispatchMessage` loop
until `ShutdownCompleted` posts `WM_QUIT`, or the experiment measures nothing.

*Does unloading give the memory back?* **No**, and that settles the design:

| | working set | private | modules |
|---|---|---|---|
| before any XAML | 36 MB | 12 MB | 58 |
| after round 1, XAML closed | 85 MB | 49 MB | 105 |
| after round 2, XAML closed | 96 MB | 62 MB | 105 |
| after round 8, XAML closed | 99 MB | 62 MB | 105 |

It is not a leak -- it plateaus after the second round and stays flat. But the module count
never falls back from 105: the XAML DLLs stay loaded, and roughly 50 MB of private bytes is
paid permanently by any process that has initialised XAML once, whether or not XAML is
currently "up". Closing `WindowsXamlManager` makes re-initialisation possible; it does not
return anything.

So in-process load-and-unload is possible and pointless. Giving the memory back was the
entire reason to want it, and it does not. **Process exit is the only unload that returns
anything**, which decides the shape after all -- now for a measured reason rather than an
assumed one:

```
dragonperch.exe     owns the tray icon, in plain Win32. Always running, ~2 MB,
                    no App SDK. Handles TaskbarCreated, because it is the thing
                    that is always there to handle it.
                       |  right-click at (x, y)
                       v
DragonPerch.Shell.exe   started on demand. Shows the Fluent MenuFlyout from a
                    transparent host window at the cursor, and answers with the
                    command chosen. Hosts the settings window too.
```

So nothing is paid until the first right-click -- which for most installations is never.

Two details make it feel instant rather than clever:

- **Pre-warm on hover.** A tray icon is sent `WM_MOUSEMOVE` through its callback message
  before it is ever clicked. Starting the shell there gives it the couple of hundred
  milliseconds a person spends moving the mouse onto an icon and pressing the button. A
  cold WinUI process needs roughly that long to show a window, and a context menu that
  arrives half a second late feels broken rather than heavy.
- **Keep it alive afterwards.** The second right-click should be immediate. An idle timeout
  that exits the shell after some minutes would return the memory, and is deliberately
  *not* in the first version: it adds a state machine whose interesting case is the shell
  exiting at the exact moment somebody clicks.

  The resting cost is now measured rather than guessed: **about 62 MB private, 99 MB
  working set** for a process that has shown XAML once, and it does not come down when the
  menu closes (see the table above). That is a real number to weigh against the race, but
  it is only paid by people who have actually opened the menu, and it is paid by a process
  that can be killed at any moment without the pets noticing. The timeout stays out of the
  first version; the number is here for whoever decides to add it.

This keeps the property that matters. The daemon is small, has no App SDK, survives the
shell dying, and still runs on its own from a terminal -- while the menu, when somebody
actually opens it, is a real WinUI flyout rather than an imitation of one.

None of this applies to Linux, where the menu costs nothing to begin with because Plasma
draws it.

#### What it took, once it was written

Built as `shell/windows/`: .NET 10, Windows App SDK 2.4.0, unpackaged and self-contained,
about six hundred lines including the Win32 interop. The shape is exactly the one above --
`Shell_NotifyIcon` in the daemon, a `MenuFlyout` in the shell, WM_COPYDATA carrying text in
both directions -- and the whole path was driven end to end before it was believed:
right-click reaches the shell, the flyout appears, and clicking *Pause* puts
`command: toggle-pause` in the daemon's log.

Three things cost real time and none of them are in any tutorial.

**A flyout cannot be shown from an element with no `XamlRoot`.** `Window.Activate()` does
not finish putting the content into a tree before it returns, so the first `ShowAt` throws
`This element does not have a XamlRoot` -- which, in a process with no console and no other
window, looks exactly like the shell failing to start. The first showing waits for the
anchor's `Loaded`; every one after it is immediate, which is another reason to keep the host
window rather than rebuild it.

**The daemon has to hand the foreground right over.** Windows will not let an arbitrary
process take the foreground, and a menu that cannot take it is shown and dismissed in the
same frame. The daemon holds the right at the moment of the click, so it calls
`AllowSetForegroundWindow` with the shell's process id before asking for the menu. This is
the real answer to "why does the daemon ask the shell, rather than the shell watching for
clicks itself".

**An old flyout's `Closed` handler will hide the host window out from under a new one.**
Showing a second flyout closes the first, and if `Closed` unconditionally hides the host,
the menu that just appeared is anchored to a window that has just been hidden. It looked
like the menu working exactly once per process. The handler now ignores anything that is
not the flyout currently on screen.

Three more, all found by somebody looking at the thing rather than at a log, and all of
them the sort that a test would have had to be written in advance to catch.

**The host window was visible.** `AppWindow.MoveAndResize` before the first `Activate()` is
thrown away -- activation applies a default size -- so the "one pixel at the cursor" was in
fact a small empty window sitting next to the menu. Resizing again after `Activate()` fixes
it; the window settles at 2x2, which is WinUI's floor and is covered by the flyout. The
presenter is also told `SetBorderAndTitleBar(false, false)`, and the flyout is given
`ShouldConstrainToRootBounds = false` so it is allowed its own top-level window rather than
being clipped to a 2-pixel one.

**Hovering opened the menu.** The pre-warm started the shell with `--menu x y` on its
command line, so a shell started by the pointer merely passing over the icon put a menu on
the screen. Pre-warming and asking for a menu are now separate things, and the daemon only
ever does the first: `shell::prewarm()` starts a silent shell, and the menu is always asked
for over WM_COPYDATA afterwards. The right-click path pre-warms too, and still shows the
Win32 menu for *that* click -- asking the new shell for one as well would put two menus on
the screen at once, which is what the first attempt at this fix did.

**The shell outlived the daemon.** It is started by the daemon but is not its child in any
sense Windows enforces, so quitting the pets left 40 MB of WinUI in the process list for
ever. The shell now finds the daemon's process id through its control window and waits on
the process handle -- not on a goodbye message, because a daemon that is killed, crashes,
or has its console closed never gets to send one. Verified three ways: `--stop`, an outright
kill, and starting the shell with no daemon at all, which exits immediately because there
is nothing for it to do.

**The app's own `.pri` is dropped by `dotnet publish` unless `EnableMsixTooling` is set** --
even for an unpackaged app, and this is the worst failure of the four because nothing says
so. `DragonPerch.Shell.pri` holds the compiled `App.xaml`, and with it the merged
`XamlControlsResources`. Without it every control silently falls back to its built-in
template: the menu still appears, still has the right items, the right icons and the right
fonts, and is no longer Fluent -- square corners, no shadow, a full-width separator. There
is no error, no warning, and no log line. `dotnet build` produces the `.pri` either way, so
a locally built shell looks correct and only the shipped one is wrong.

It was caught by looking at a screenshot, and it was nearly blamed on Native AOT, which had
just been turned on. The two were separated by publishing both ways and comparing the
images pixel for pixel: identical. Neither the toolchain nor the plausible culprit, but a
missing 40 KB file.

**Size, and Native AOT.** The first self-contained build was 222 MB, which the plan's
"forty-odd megabytes" did not anticipate. Two changes took it to **62 MB**, and one wrong
turn is worth recording with them.

*Reference the modular packages, not the umbrella.* `Microsoft.WindowsAppSDK` is a
metapackage over ten others, four of which -- AI, ML, Search, Widgets -- a tray menu has no
use for; `onnxruntime.dll` and `DirectML.dll` alone were 38 MB. There is no property to
switch any of them off. Naming `Microsoft.WindowsAppSDK.WinUI` and
`Microsoft.WindowsAppSDK.Runtime` instead is the supported way, and it is safe because
WinUI depends on Base, Foundation and InteractiveExperiences but on none of the four.

An earlier draft of this section said the modular packages were `-experimental` and
therefore unusable. **That was wrong**, and wrong in an avoidable way: it came from asking
each package for its newest version, which is indeed an `-experimental` one, rather than
reading which versions the 2.4.0 umbrella actually pins. Those are all stable releases, and
they are what this project now names.

*Native AOT works, and suits this program unusually well.* `PublishAot` turns the managed
side -- a CLR, `System.Private.CoreLib`, and a 25 MB Windows SDK projection -- into one
4.6 MB executable. More to the point, a cold start measured through the tray icon fell from
seconds to **73 ms**, and resting memory from 84 MB private to 43 MB. For a process whose
entire job is to put a menu on the screen between somebody deciding to right-click and
finishing the click, that is the number that matters.

The native linker's symbols are another 21 MB. `NativeDebugSymbols=false` does not stop
them, removing them from `ResolvedFileToPublish` does not either -- AOT symbols are copied
by the native-binary targets -- and a `Delete` after `Publish` is undone. CI drops them when
it builds the zip, which is arguably where that belongs: worth having beside a local build,
not worth 21 MB in a download.

CI ships the shell as its own zip regardless, because nobody who only wants the pets should
download it.


### 13.4 Settings (milestone 10)

§8 chose the shape and it holds: **a separate program per platform**, so each gets its
native toolkit without imposing one on the daemon.

#### The file, and the part that is shared -- **done**

INI, at `~/.config/dragonperch/dragonperchrc` and
`%APPDATA%\DragonPerch\dragonperchrc`. INI because KConfig writes it, which is what makes
the Linux settings program able to use KConfig rather than a parser of its own; the same
file name on both platforms because there is no reason for them to differ and one name is
one thing to document.

`parse_sections` in `sprite_pack_file.cpp` turned out to be generic with only its caller
pack-specific, and is now `dragonperch/ini.hpp` with both callers on it.

`dragonperch/settings.hpp` holds the `Settings` struct, `parse_settings` and
`write_settings`. Three things about it are deliberate:

- **`Settings` is not `SimulationOptions`.** That struct is the physics' own vocabulary and
  carries knobs -- terminal velocity, the chance of turning at an edge -- meant for whoever
  writes the simulation, not whoever runs it. `to_options()` is the narrow gate between
  them, and a test asserts that nothing else gets through.
- **Nothing in it throws.** A value that cannot be read keeps its default and a file that
  will not parse at all is every default. This file is edited by hand and written by two
  other programs; a dragon that never appears because of a stray bracket is worse than one
  that appears with the defaults.
- **`std::strtod`, not `std::from_chars`.** Reading one `double` with `from_chars` drags in
  the Ryu conversion tables -- the same 118 KB that `dragonperch/text.hpp` exists to keep
  out. `strtod` is an import against the C runtime. `two_places()` formats the value back
  out for the same reason.

Applying a change without a restart needed two additions to `Simulation`, both now present:
`set_options` (which preserves the seed, so adjusting a speed does not re-randomise
everyone) and `clear_pets`.

Where the file lives is the one platform-specific part, so it is `settings_file.cpp` in
each head. `Settings::needs_respawn` decides which of the two kinds of reload a change
calls for: adjusting a walk speed while a pet is mid-stride must not teleport it, and
changing which mascots exist cannot be done any other way. The two heads apply it
differently, and the difference is the same one as for pausing -- on Windows the handlers
run on the render thread and reach the simulation directly; on Wayland they run on the bus
thread and set `g_reload`, which the render loop reads between frames.

`--pets N` now means the same thing as `pets-per-mascot` and overrides it. It used to mean
a total shared out between the mascots round-robin, which made `--pets 2` with three
mascots a puzzle. With one of each as the default, the no-argument case is three dragons
either way.

#### Windows: the same shell process, WinUI 3, in C# -- **done**

The settings window is a page in `DragonPerch.Shell.exe`, the program §13.3 gives the tray
icon to. One WinUI application rather than two, and the tray's Settings item opens a window
it already owns instead of starting a process. It costs 0.6 MB on top of the menu.

Nothing is applied until Apply is pressed; then the file is written and the daemon is sent
`reload` on the same control interface `--reload` uses. Verified end to end by unticking a
mascot and moving the speed slider: the file came back
`mascots = konqi, kori` / `walk-speed = 117.00`, and the daemon's log went
`command: reload` -> `2 pet(s) after reload`, down from three.

Two lists behave the same way and the cards say so: all ticked saves as the *empty* list,
because empty means "all of them" to the daemon and a list written today should not exclude
a mascot installed tomorrow. The mascots themselves are read from `assets/` beside the
executable rather than hard-coded, for the same reason.

**Not the Windows Community Toolkit.** `SettingsCard` was the plan and was tried: it pins
Microsoft.WindowsAppSDK 1.6, which collides with the 2.x packages the shell uses -- duplicate
imports and a hard error out of the MSIX build tools. Going back to the 1.6 umbrella to get
it would undo the 38 MB the shell just saved. The page is built from WinUI's own controls
and the system's own theme resources (`CardBackgroundFillColorDefaultBrush`,
`CardStrokeColorDefaultBrush`, `ControlCornerRadius`), which is where SettingsCard gets its
appearance from anyway -- so it still follows the user's theme, accent colour and contrast
settings with no code.

Two failures worth writing down, because both build cleanly and neither says anything:

- **A `Style` cannot contain a `Setter` for the `Style` property.** It throws
  `InvalidCastException` when the window is constructed. Use `BasedOn`.
- **`foreach` over a WinRT-projected `IReadOnlyList` throws under Native AOT.**
  `DisplayArea.FindAll()` returns one, and asking it for an enumerator dies inside CsWinRT's
  `Make_IEnumerableObjRef` -- the generic instantiation was never generated. Indexing works.
  This is the first thing in this program that AOT actually broke, and it was found by
  logging `e.ToString()` rather than by reading the markup: the message alone says
  "Specified cast is not valid" and nothing else.

The cost of all this is real and worth naming: **a second build system.** CMake cannot
sensibly build a WinUI project, so `shell/windows/` is a `.csproj` built by `dotnet publish`
and invoked separately by CI. The C++ solution stays C++, and the Windows App SDK never
touches the daemon.

And a second implementation of the settings file. `Settings.cs` reads and writes the same
INI as `dragonperch/settings.cpp`, with the same defaults, the same clamping and the same
refusal to throw. That duplication is inherent rather than accidental -- the Linux settings
program is a KCM and will use KConfig -- so **the file is the contract and the code cannot
be shared**. It is currently checked by running both against one file, which is how the
numbers above were obtained; a C# test project asserting the round trip is worth adding and
is not there yet.

#### Linux: Kirigami, as a KCM

Build it as `kcm_dragonperch`, a KDE Config Module: Qt6, KF6 (`KCMUtils`, `KConfig`,
`KI18n`), Kirigami for the QML, built with `extra-cmake-modules`.

A KCM rather than a standalone window because `kcmshell6 kcm_dragonperch` gives the
standalone case for free -- which is what the tray's Settings item launches -- while also
turning up in System Settings where a KDE user would look for it first. One target, both
places.

It adds Qt6 and KF6 as build dependencies, so it is `-DDRAGONPERCH_BUILD_KCM=ON`, off by
default and on in the packaging build. The daemon must keep building with neither.

#### What is worth settling

| Setting | Why |
|---|---|
| How many pets, and which mascots | The first thing anyone wants to change |
| Walk speed, idle frequency | `SimulationOptions` already carries these |
| Which monitors to use | A pet wandering onto a television is not wanted |
| Start with the session | Currently a manual copy into `~/.config/autostart` |
| Pause while a full-screen app is running | Milestone 11; implemented on Windows, and on Wayland only the KWin script can see it |
