# SPDX-License-Identifier: GPL-3.0-or-later
#
# What `cpack` builds on Linux: a .deb to install, and a .tar.gz to unpack anywhere.
#
# Both come out of the same `install()` rules, so what is tested is what is shipped. The
# tarball exists because it is the quicker thing to try on a virtual machine -- unpack,
# run, delete -- while the .deb is what a person would actually keep.

include(GNUInstallDirs)

set(CPACK_PACKAGE_NAME dragonperch)
set(CPACK_PACKAGE_VENDOR "DragonPerch contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION "${DRAGONPERCH_FULL_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# dragonperch_0.1.0~20260828.1830.g462431a_x86_64.tar.gz rather than the Linux-x86_64 CMake
# would otherwise pick. The version is in the name so that two nightlies sitting in a
# downloads folder can be told apart without opening them.
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}_${CPACK_PACKAGE_VERSION}_${CMAKE_SYSTEM_PROCESSOR}")

set(CPACK_GENERATOR "DEB;TGZ")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
set(CPACK_DEBIAN_PACKAGE_SECTION "x11")
# name_version_arch.deb, the Debian convention. Note that this name is not what apt reads:
# the version it compares comes from the control field, which is why GitHub rewriting the
# tilde out of a release asset's name costs nothing. CI prints the control field on every
# run so the two can be seen not to matter.
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Worked out by dpkg-shlibdeps from what the binary actually links, rather than written
# out by hand: a hand-written list is a list of library versions that will be wrong on the
# next release, and getting it wrong produces a package that installs and then will not
# start.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# ---------------------------------------------------------------------------------------
# Two packages, because two of the things installed here belong to one desktop and the rest
# belong to none.
#
# `dragonperch` is the daemon, the artwork, the translations and the desktop entry: nothing
# in it knows what a KWin is. `dragonperch-kde` is the KWin script and the settings module,
# which are Plasma's and only Plasma's.
#
# Split now rather than when it hurts. The Wayland head already has to learn a second way of
# finding out where the windows are for anything that is not KWin -- GNOME would need a
# Shell extension, wlroots has swaymsg and hyprctl -- and each of those brings its own
# desktop-shaped baggage. A single package that grows a Recommends for every desktop in
# existence is the shape this is avoiding.
#
# DEB only. The tarball stays one file: "unpack anywhere and run" is the whole point of it,
# and handing somebody two archives to unpack in the right order is not that.
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL core kde)

set(CPACK_DEBIAN_CORE_PACKAGE_NAME dragonperch)
set(CPACK_DEBIAN_KDE_PACKAGE_NAME dragonperch-kde)

set(CPACK_COMPONENT_CORE_DESCRIPTION "${PROJECT_DESCRIPTION}")
set(CPACK_COMPONENT_KDE_DESCRIPTION
    "Plasma integration for DragonPerch: the KWin script that tells it where the windows "
    "are, and the settings module in System Settings.")

# The KDE half needs the daemon it configures, and exactly the build of it: the two speak a
# settings file and a D-Bus interface that are versioned together.
set(CPACK_DEBIAN_KDE_PACKAGE_DEPENDS "dragonperch (= ${DRAGONPERCH_FULL_VERSION})")

# Recommends, not Depends, and now on the package that is actually about Plasma. Without
# the KWin script the pets have nothing to stand on but the floor -- but a person who
# installs this package on a machine that has no KWin yet should get a package, not an
# error.
set(CPACK_DEBIAN_KDE_PACKAGE_RECOMMENDS "kwin-wayland | kwin-x11")

# What the settings module needs beyond what it links. Its Qt and KDE libraries are picked
# up by dpkg-shlibdeps; these two are not, and could not be. `org.kde.kirigami` is a QML
# module loaded by name at run time -- nothing links it -- and kcmshell6 is the program the
# tray's Settings item runs, which the daemon only ever names as a string.
if(DRAGONPERCH_BUILD_KCM)
    string(APPEND CPACK_DEBIAN_KDE_PACKAGE_RECOMMENDS
        ", qml6-module-org-kde-kirigami, libkf6kcmutils-bin")
endif()

# And the daemon suggests it, so that `apt install dragonperch` on a Plasma desktop offers
# the half that makes it work rather than leaving somebody to find out.
set(CPACK_DEBIAN_CORE_PACKAGE_SUGGESTS "dragonperch-kde")

include(CPack)
