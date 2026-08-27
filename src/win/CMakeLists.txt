# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(dragonperch WIN32
    main.cpp
    overlay_window.cpp
    dcomp_renderer.cpp
    console.cpp
    log.cpp
    dragonperch.rc)

target_link_libraries(dragonperch PRIVATE
    DragonPerch::core
    d3d11
    dxgi
    dcomp
    d2d1
    dwmapi
    user32
    shell32)

target_include_directories(dragonperch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(dragonperch PRIVATE DRAGONPERCH_VERSION="${PROJECT_VERSION}")

# The manifest arrives through dragonperch.rc, so stop the linker generating one of its
# own and invoking mt.exe over it.
target_link_options(dragonperch PRIVATE /MANIFEST:NO)

# WIN32 above makes this a GUI subsystem binary, so it has no console of its own: a stray
# console window flashing on login is not acceptable for something that autostarts. The
# diagnostic modes borrow the launching shell's console instead, see console.cpp.
