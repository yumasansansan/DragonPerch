# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28)

project(DragonPerch
    VERSION 0.1.0
    DESCRIPTION "Konqi and friends romp across your window title bars and panels"
    HOMEPAGE_URL "https://github.com/yumasansansan/DragonPerch"
    LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keeps clangd and other tooling working on the Linux side.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor)
    add_compile_definitions(
        UNICODE _UNICODE
        NOMINMAX                    # or windows.h steals std::min / std::max
        WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Visual Studio: start debugging the Windows head rather than ALL_BUILD.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT dragonperch)

add_subdirectory(src/core)

if(WIN32)
    add_subdirectory(src/win)
elseif(UNIX AND NOT APPLE)
    # add_subdirectory(src/linux)   # milestone 6
endif()

include(CTest)
if(BUILD_TESTING)
    # add_subdirectory(tests)       # milestone 2
endif()
