# SPDX-License-Identifier: GPL-3.0-or-later
#
# HARD RULE: nothing in this target may include a platform header. If a file here
# mentions Windows, X11, Wayland or COM, the port has already gone wrong. CI builds this
# target on its own to keep that honest.

add_library(dragonperch_core STATIC
    dragonperch/edge_builder.cpp
    dragonperch/geometry.cpp
    dragonperch/host.cpp
    dragonperch/ini.cpp
    dragonperch/pack_library.cpp
    dragonperch/placeholder_pack.cpp
    dragonperch/settings.cpp
    dragonperch/simulation.cpp
    dragonperch/sprite_pack_file.cpp
    dragonperch/sprites.cpp
    dragonperch/world.cpp)

target_include_directories(dragonperch_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Two different kinds of requirement, which is why they are stated separately.
#
# The language standard is a *usage requirement*: anything that includes these headers has
# to compile as C++23, so it propagates.
target_compile_features(dragonperch_core PUBLIC cxx_std_23)

# Warnings and optimisation are *our build policy*, and PRIVATE on purpose. They were
# PUBLIC, which meant every consumer inherited -Werror -- including the test binary, where
# Catch2's use of __COUNTER__ is diagnosed by Clang 22 as a C2y extension and killed the
# build. Our code is held to the policy; code that merely links us is not.
target_link_libraries(dragonperch_core PRIVATE DragonPerch::options)

add_library(DragonPerch::core ALIAS dragonperch_core)
