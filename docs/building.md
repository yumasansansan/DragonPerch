<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Building from source

You do not need this to use DragonPerch: the [packages](packages.md) are built by CI
from the same rules. This is for changing it.

## The daemon

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
sudo apt install clang-22 lld-22 cmake ninja-build libwayland-bin libwayland-dev libwayland-egl-backend-dev libegl-dev libgles-dev libpng-dev libsystemd-dev pkg-config
```

```bash
cmake --preset linux-x64 && cmake --build --preset linux-x64-debug
```

Use the preset rather than a hand-written `cmake -G Ninja`, and note the environment
variable for the C compiler is `CC`, not `C` — set the wrong one and CMake silently picks
whatever `cc` happens to be while using Clang for C++.

**Clang and LLD are build dependencies, and the newest ones installed are the ones used.**
The Linux preset points at [cmake/ClangLatest.cmake](../cmake/ClangLatest.cmake), which walks
`clang++-19` … `clang++-40` and takes the highest that is present, then also asks an
unsuffixed `clang++` its version in case that is newer still. `clang-22` in the line above
is a version that is known to work, not a requirement: install 23 instead and the build
picks it up with no edit anywhere. Ubuntu itself has no `clang-23`, so the newest ones come
from [apt.llvm.org](https://apt.llvm.org/) — CI adds that archive and takes the highest
numbered `llvm-toolchain-<codename>-NN` suite it offers, which is one way to get them.

Plain `clang` is deliberately not what gets used. On Ubuntu 26.04 the unsuffixed name is
21 while 22 is installed beside it, so asking for it would silently build with the older
compiler — which is the thing this is here to prevent, not a portability nicety to fall
back on.

The linker is `lld` **of the compiler's own version**, derived rather than written down,
and a hard error if it is missing. Release builds are ThinLTO, and the bitcode a compiler
emits is only guaranteed readable by its own version's linker; pairing Clang 22 with an
LLD from 21 is a link failure at best and a silently non-LTO Release at worst. There is no
fall back to GNU ld.

`external/` holds `wayland-protocols` and `wlr-protocols` as submodules rather than copies
of the two XML files, so that where each came from is recorded and updating is one command.
`wayland-scanner` turns them into C at build time — a Wayland protocol is a data file, not
a library, so there is nothing to link against.

Ninja is the generator on Linux because there is no solution to open there -- it is a build
executor, the counterpart to MSBuild, and CMake writes its input. On Windows the Visual
Studio generator does that job, so there is deliberately no Ninja preset for Windows.

### Compiler and linker flags

All of them live in [cmake/CompilerOptions.cmake](../cmake/CompilerOptions.cmake), on one
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

## The optional pieces

Three things in this tree are built separately, and the daemon has to keep building when
none of them are present.

**The Windows shell** — the Fluent tray menu and settings window — is C# and is deliberately
not wired into CMake: CMake cannot sensibly build a WinUI project, and the daemon must build
with no .NET anywhere near it. It needs the .NET 10 SDK.

```bash
dotnet publish shell/windows/DragonPerch.Shell.csproj -c Release -o dist/shell
```

Publish rather than build, always. `dotnet build` does not run the Native AOT compiler, so
what it produces behaves differently from what ships — a cast that works in a `dotnet build`
output has already thrown `E_NOINTERFACE` in the published one and taken the tray menu with
it. Measure on the published binary or do not measure.

**The KDE settings module** is the only thing here that wants Qt and KF6:

```bash
sudo apt install qt6-base-dev qt6-declarative-dev extra-cmake-modules libkf6config-dev libkf6coreaddons-dev libkf6i18n-dev libkf6kcmutils-dev
```

```bash
cmake --preset linux-x64 -D DRAGONPERCH_BUILD_KCM=ON && cmake --build --preset linux-x64-debug
```

**The fuzz targets** are built by `DRAGONPERCH_SANITIZE=ON`, which is what libFuzzer needs
anyway. [The fuzz targets](../fuzz/README.md) says what each one asserts and how to run one.

| Option | Default | What it does |
|---|---|---|
| `DRAGONPERCH_DIAGNOSTICS` | `OFF` in Release | Keeps `--dump-world` and the other probes in a Release build |
| `DRAGONPERCH_SANITIZE` | `OFF` | ASan, UBSan, and the fuzz targets |
| `DRAGONPERCH_BUILD_KCM` | `OFF` | The KDE settings module |
| `DRAGONPERCH_TIDY` | `OFF` | Runs clang-tidy over every translation unit as it builds |

## Tests

```bash
ctest --test-dir build/windows-x64 --build-config Debug --output-on-failure
```

The simulation takes a world snapshot and a delta time and produces sprite positions, and a
fake world is just a list of line segments — so the physics is tested with no compositor,
no windows and no platform involved at all. Occlusion clipping is tested the same way: it
is rectangles and a stacking order, and it is shared by both backends.
