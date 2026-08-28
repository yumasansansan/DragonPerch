# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28)

project(DragonPerch
    VERSION 0.1.0
    DESCRIPTION "Konqi and friends romp across your window title bars and panels"
    HOMEPAGE_URL "https://github.com/yumasansansan/DragonPerch"
    LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
elseif(UNIX AND NOT APPLE)
    # wayland-scanner emits C, and the project is otherwise C++ only. Enabling a language
    # has to happen at the top level, not in the subdirectory that needs it.
    enable_language(C)
endif()

# What a nightly build appends to the version, so that one package can be told from the
# next. Empty for a release; CI passes something like "~20260828.1830.g462431a".
#
# The leading tilde is the load-bearing character. Debian sorts `~` before everything,
# including the empty string, so 0.1.0~20260828.1830 < 0.1.0~20260829.0300 < 0.1.0 -- which
# means apt upgrades one nightly to the next, and a real 0.1.0 supersedes every nightly of
# it. Written any other way round, `apt install ./dragonperch_*.deb` refuses the newer file
# as a downgrade.
set(DRAGONPERCH_VERSION_SUFFIX "" CACHE STRING "Appended to the version for nightly builds")
set(DRAGONPERCH_FULL_VERSION "${PROJECT_VERSION}${DRAGONPERCH_VERSION_SUFFIX}")
message(STATUS "DragonPerch version: ${DRAGONPERCH_FULL_VERSION}")

# So that a packaging step can read the version rather than work it out again. Asking the
# built binary would be the obvious thing and is not reliable on Windows, where this is a
# GUI-subsystem program and its standard output depends on how it was launched.
file(WRITE ${CMAKE_BINARY_DIR}/version.txt "${DRAGONPERCH_FULL_VERSION}")

# The diagnostic modes -- --probe-composition, --dump-world, --self-test and
# --export-placeholder -- are compiled in for Debug and left out of Release.
#
# They are not dead weight: every hard bug in this project was found by one of them, and
# they exist to be run against the real thing. So this is a switch rather than an #ifndef
# NDEBUG, and turning it on gives a Release binary with the diagnostics in -- which is what
# to send somebody when a shipped build misbehaves, rather than a Debug build that has
# different timing and a different D2D layer and so is answering a different question.
option(DRAGONPERCH_DIAGNOSTICS "Include the diagnostic modes in Release builds too" OFF)

# A generator expression, not an if(CMAKE_BUILD_TYPE). Both generators here pick the
# configuration at build time, so testing it at configure time silently does nothing --
# the same trap as the optimisation flags in cmake/CompilerOptions.cmake.
set(DRAGONPERCH_DIAGNOSTICS_CONDITION
    $<OR:$<CONFIG:Debug>,$<BOOL:${DRAGONPERCH_DIAGNOSTICS}>>)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(UNIX AND NOT APPLE)
    include(WaylandProtocol)
endif()

# Every compiler and linker flag lives in one place. See cmake/CompilerOptions.cmake, which
# defines the DragonPerch::options interface target that each real target links.
include(CompilerOptions)

# Keeps clangd and other tooling working on the Linux side.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Visual Studio: start debugging the Windows head rather than ALL_BUILD.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT dragonperch)

add_subdirectory(src/core)

if(WIN32)
    add_subdirectory(src/win)
elseif(UNIX AND NOT APPLE)
    add_subdirectory(src/linux)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)

    # The artwork. Found relative to the executable rather than through a configure-time
    # path, so /usr/bin/dragonperch-wl looks in /usr/share/dragonperch and the same binary
    # unpacked into a home directory still finds it -- see find_sprite_packs.
    install(DIRECTORY assets/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dragonperch/assets
        FILES_MATCHING PATTERN "*.ini" PATTERN "*.png"
        PATTERN "source" EXCLUDE)

    # Into KWin's own script directory, so it appears in System Settings -> Window
    # Management -> KWin Scripts and only has to be ticked. It is still not enabled by
    # installing it: a package that starts running code inside somebody's compositor
    # without being asked would deserve everything it got.
    install(DIRECTORY kwin/dragonperch-geometry
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/scripts)

    install(FILES packaging/dragonperch.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

    # Beside the artwork, because that is where the program looks: the tray sends the icon
    # over D-Bus as pixels, so it has to be found whether or not anything was installed.
    install(FILES packaging/dragonperch.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dragonperch)

    # And into the icon theme as well, so the .desktop entry's Icon=dragonperch resolves
    # and a desktop that would rather use a themed name than our pixels can.
    install(FILES packaging/dragonperch.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/64x64/apps)

    include(Packaging)
endif()
