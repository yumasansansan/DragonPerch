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
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Worked out by dpkg-shlibdeps from what the binary actually links, rather than written
# out by hand: a hand-written list is a list of library versions that will be wrong on the
# next release, and getting it wrong produces a package that installs and then will not
# start.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# Recommends, not Depends. The KWin script is how DragonPerch finds out where the windows
# are, and without it the pets have nothing to stand on but the floor -- but the package
# still installs and runs, and a hard dependency on the whole of Plasma for a program that
# also has a Windows head would be wrong.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "kwin-wayland | kwin-x11")

include(CPack)
