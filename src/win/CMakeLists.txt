# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(dragonperch WIN32
    main.cpp
    desktop_scanner.cpp
    win_event_watcher.cpp
    overlay_window.cpp
    frame_clock.cpp
    fullscreen.cpp
    gpu_device.cpp
    output_surface.cpp
    png.cpp
    sprite_pack_loader.cpp
    self_test.cpp
    sprite_renderer.cpp
    console.cpp
    log.cpp
    dragonperch.rc)

target_link_libraries(dragonperch PRIVATE
    DragonPerch::core
    DragonPerch::options
    d3d11
    dxgi
    dcomp
    d2d1
    dwmapi
    user32
    shell32
    shcore       # GetDpiForMonitor
    windowscodecs
    ole32)       # WIC

target_include_directories(dragonperch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# On this target, not globally. These are Windows API settings and only this head touches
# the Windows API -- and leaking _UNICODE into everything else broke the test binary, which
# picked up a wmain entry point from Catch2 while the linker was looking for main.
target_compile_definitions(dragonperch PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX                # or windows.h steals std::min and std::max
    WIN32_LEAN_AND_MEAN)
target_compile_definitions(dragonperch PRIVATE DRAGONPERCH_VERSION="${PROJECT_VERSION}")

# The manifest arrives through dragonperch.rc, so stop the linker generating one of its
# own and invoking mt.exe over it.
target_link_options(dragonperch PRIVATE /MANIFEST:NO)

# WIN32 above makes this a GUI subsystem binary, so it has no console of its own: a stray
# console window flashing on login is not acceptable for something that autostarts. The
# diagnostic modes borrow the launching shell's console instead, see console.cpp.
