# SPDX-License-Identifier: GPL-3.0-or-later

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-egl)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl glesv2)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)

# sd-bus, for the KWin geometry script. It lives in libsystemd rather than in a library of
# its own; libelogind provides the same API on systems without systemd, and the header and
# the calls are identical, which is why only the module name is conditional here.
pkg_check_modules(SDBUS IMPORTED_TARGET libsystemd)
if(NOT SDBUS_FOUND)
    pkg_check_modules(SDBUS REQUIRED IMPORTED_TARGET libelogind)
endif()

# One library for the protocol bindings, so the warning policy below does not apply to
# machine-generated code. wayland-scanner's output is correct but not written to our
# taste, and -Werror over it would make every protocol update a build break.
add_library(dragonperch_wayland_protocols STATIC)
dragonperch_add_wayland_protocol(dragonperch_wayland_protocols
    xdg-shell ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
dragonperch_add_wayland_protocol(dragonperch_wayland_protocols
    wlr-layer-shell-unstable-v1
    ${WLR_PROTOCOLS_DIR}/unstable/wlr-layer-shell-unstable-v1.xml)
target_link_libraries(dragonperch_wayland_protocols PUBLIC PkgConfig::WAYLAND)

# xdg-shell is not used -- DragonPerch never opens a popup or a toplevel. It is generated
# because zwlr_layer_surface_v1.get_popup takes an xdg_popup, so the layer-shell header
# refers to xdg_popup_interface and will not link without it.

add_executable(dragonperch-wl
    main.cpp
    wayland_display.cpp
    layer_surface.cpp
    egl_context.cpp
    gles_renderer.cpp
    frame_clock.cpp
    kwin_geometry.cpp
    kwin_script.cpp
    sprite_pack_loader.cpp
    png.cpp
    log.cpp)

target_link_libraries(dragonperch-wl PRIVATE
    DragonPerch::core
    DragonPerch::options
    dragonperch_wayland_protocols
    PkgConfig::WAYLAND
    PkgConfig::EGL
    PkgConfig::SDBUS
    PkgConfig::PNG)

target_include_directories(dragonperch-wl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(dragonperch-wl PRIVATE DRAGONPERCH_VERSION="${DRAGONPERCH_FULL_VERSION}")
target_compile_definitions(dragonperch-wl
    PRIVATE $<${DRAGONPERCH_DIAGNOSTICS_CONDITION}:DRAGONPERCH_DIAGNOSTICS>)

# No RPATH, and none to strip out at install time either. Everything this links is a
# system library already on the loader's default path, so an RPATH could only ever be a
# path from the build machine baked into the binary.
#
# Not cosmetic: without this, CMake wants to relink on install to change the RPATH, and
# with the plain Ninja generator that step is refused outright -- "requires changing an
# RPATH from the build tree, but this is not supported with the Ninja generator".
set_target_properties(dragonperch-wl PROPERTIES
    SKIP_BUILD_RPATH TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "")

include(GNUInstallDirs)
install(TARGETS dragonperch-wl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
