# SPDX-License-Identifier: GPL-3.0-or-later
#
# Turns Wayland protocol XML into C, the way every Wayland client is built.
#
# A protocol is a data file, not a library: `wayland-scanner` reads the XML and writes a
# header of opcodes and inline request wrappers plus a translation unit of interface
# tables. There is nothing to link against and nothing to find -- the XML is the ABI.
#
# The XML itself comes from the two upstream repositories, as submodules under external/.
# A copy in-tree would have to be updated by hand when upstream moves; a submodule records
# where each file came from and which commit, and updates in one command. It does still pin
# a commit, so updating stays a deliberate act -- which for a frozen protocol is what you
# want anyway.

find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)

set(WAYLAND_PROTOCOLS_DIR ${CMAKE_SOURCE_DIR}/external/wayland-protocols)
set(WLR_PROTOCOLS_DIR ${CMAKE_SOURCE_DIR}/external/wlr-protocols)

if(NOT EXISTS ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
    message(FATAL_ERROR
        "external/wayland-protocols is empty. The protocol XML lives in submodules:\n"
        "    git submodule update --init --depth 1\n")
endif()

# Adds one protocol to `target`. `xml` is an absolute path; `stem` names the generated
# files. Both the client header and the code go into the build tree, never the source tree.
function(dragonperch_add_wayland_protocol target stem xml)
    set(header ${CMAKE_CURRENT_BINARY_DIR}/wayland-generated/${stem}-client-protocol.h)
    set(code ${CMAKE_CURRENT_BINARY_DIR}/wayland-generated/${stem}-protocol.c)

    # The rename pass is not optional for C++. Wayland protocols are specified for C, and
    # `zwlr_layer_shell_v1.get_layer_surface` takes an argument called `namespace` -- so the
    # header wayland-scanner writes does not compile here at all until it is renamed. See
    # RenameCxxKeywords.cmake.
    set(rename ${CMAKE_SOURCE_DIR}/cmake/RenameCxxKeywords.cmake)

    add_custom_command(
        OUTPUT ${header}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/wayland-generated
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${xml} ${header}
        COMMAND ${CMAKE_COMMAND} -DHEADER=${header} -P ${rename}
        DEPENDS ${xml} ${rename}
        COMMENT "wayland-scanner client-header ${stem}"
        VERBATIM)

    # `private-code` rather than `public-code`: the interface tables are linked into this
    # binary and nothing else, so they must not be exported.
    add_custom_command(
        OUTPUT ${code}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/wayland-generated
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code ${xml} ${code}
        DEPENDS ${xml}
        COMMENT "wayland-scanner private-code ${stem}"
        VERBATIM)

    target_sources(${target} PRIVATE ${header} ${code})

    # SYSTEM, so that our warning policy does not apply to a machine-written header. The
    # project builds with -Werror, and a future protocol update tripping a warning we
    # cannot fix in a file we do not write would break the build for no reason.
    target_include_directories(${target}
        SYSTEM PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/wayland-generated)
endfunction()
