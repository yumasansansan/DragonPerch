# SPDX-License-Identifier: GPL-3.0-or-later
#
# HARD RULE: nothing in this target may include a platform header. If a file here
# mentions Windows, X11, Wayland or COM, the port has already gone wrong. CI builds this
# target on its own to keep that honest.

add_library(dragonperch_core STATIC
    dragonperch/geometry.cpp
    dragonperch/placeholder_pack.cpp
    dragonperch/simulation.cpp
    dragonperch/sprites.cpp
    dragonperch/world.cpp)

target_include_directories(dragonperch_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# PUBLIC, not PRIVATE: the standard and the warning policy have to reach anything that
# links this, or a head could quietly build the shared headers under different rules.
target_link_libraries(dragonperch_core PUBLIC DragonPerch::options)

add_library(DragonPerch::core ALIAS dragonperch_core)
