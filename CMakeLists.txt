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
