# SPDX-License-Identifier: GPL-3.0-or-later
#
# Catch2 comes through FetchContent rather than a package manager. It is the only external
# dependency in the project, and pulling it in from CMake keeps the "no setup step"
# property the CI has: `cmake --preset` is still the first and only command.

include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL)

FetchContent_MakeAvailable(Catch2)

add_executable(dragonperch_tests
    simulation_test.cpp
    sprites_test.cpp
    world_test.cpp)

# Deliberately not DragonPerch::options. The project builds with warnings as errors and
# without RTTI, and neither belongs on a test binary: a Catch2 header tripping /W4 would
# fail a build that has nothing wrong with it.
target_link_libraries(dragonperch_tests PRIVATE
    DragonPerch::core
    Catch2::Catch2WithMain)

target_compile_features(dragonperch_tests PRIVATE cxx_std_23)

include(Catch)
catch_discover_tests(dragonperch_tests)
