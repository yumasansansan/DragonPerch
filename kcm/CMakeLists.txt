# SPDX-License-Identifier: GPL-3.0-or-later
#
# The settings module. Off unless asked for, because it is the only thing in this project
# that wants Qt and KDE Frameworks, and the daemon has to keep building on a machine with
# neither.

find_package(ECM ${DRAGONPERCH_ECM_MINIMUM} NO_MODULE REQUIRED)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

# KDE's install directories, told not to take their values from the CMAKE_INSTALL_* ones.
#
# ECM warns once per directory -- thirteen times -- when those are already set, because
# inheriting them is usually a mistake. They are set here: the project's own top level and
# src/linux both include GNUInstallDirs long before this directory is reached, and so does
# find_package(Qt6). The documented way to say "that is not what I meant" is this variable,
# which makes ECM ignore them rather than copy them.
#
# Nothing is lost by it. The two sets are for different things: CMAKE_INSTALL_* is where the
# daemon and the artwork go, KDE_INSTALL_* is where a plugin and a .desktop file go, and only
# kcmutils reads the second.
set(KDE_INSTALL_DIRS_NO_CMAKE_VARIABLES TRUE)
include(KDEInstallDirs)
include(KDECMakeSettings)

# Everything installed from this directory belongs to the KDE package. kcmutils writes its
# own install() rules -- the plugin and the generated .desktop file -- and there is no way
# to pass a component through them, so the default is set instead. Directory scoped, so
# nothing outside this file is affected.
#
# After the includes, not before, and that is the whole of it: KDEInstallDirs sets this
# variable to the project name unconditionally, so setting it first was setting it and
# having it taken away. The files went into a component called `DragonPerch`, which is in
# neither package, and the .deb that should have held the settings module held the KWin
# script and nothing else.
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME kde)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Quick)
find_package(KF6 REQUIRED COMPONENTS CoreAddons KCMUtils)

# The core is a static library, and a plugin is a shared object: without this the link
# fails on relocations against the core's objects rather than on anything to do with the
# code in this directory.
set_target_properties(dragonperch_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

kcmutils_add_qml_kcm(kcm_dragonperch SOURCES
    kcm_dragonperch.cpp
    # The path and the file. Compiled in rather than reimplemented so that the module and
    # the daemon cannot disagree about where the settings live or what they say.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/linux/settings_file.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/linux/log.cpp
)

target_include_directories(kcm_dragonperch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/linux
)

target_link_libraries(kcm_dragonperch PRIVATE
    dragonperch_core
    Qt6::DBus
    Qt6::Gui
    Qt6::Quick
    KF6::CoreAddons
    KF6::KCMUtilsQuick
)
