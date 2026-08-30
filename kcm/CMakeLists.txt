# SPDX-License-Identifier: GPL-3.0-or-later
#
# The settings module. Off unless asked for, because it is the only thing in this project
# that wants Qt and KDE Frameworks, and the daemon has to keep building on a machine with
# neither.

find_package(ECM ${DRAGONPERCH_ECM_MINIMUM} NO_MODULE REQUIRED)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Quick)
find_package(KF6 REQUIRED COMPONENTS CoreAddons I18n KCMUtils)

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
    KF6::I18n
    KF6::KCMUtilsQuick
)
