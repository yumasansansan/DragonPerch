# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fuzz targets for the places text arrives from outside this program.
#
# Only built with DRAGONPERCH_SANITIZE=ON, and that is not an arbitrary coupling: the
# sanitizer options already compile everything with `-fsanitize=...,fuzzer-no-link`, which
# is exactly the instrumentation a fuzz target needs and the `-no-link` half says "some
# other target will supply main()". These are those targets, and they add `-fsanitize=fuzzer`
# at link time to get libFuzzer's main and the coverage runtime.
#
# A fuzzer with no sanitizer under it is close to pointless -- it would find hangs and
# nothing else -- so there is no configuration where these are built without one.

if(NOT DRAGONPERCH_SANITIZE OR MSVC)
    return()
endif()

# ---------------------------------------------------------------------------------------
# The targets
# ---------------------------------------------------------------------------------------
#
# `dragonperch_fuzz_<name>` so `--target` completion groups them, and a `fuzzers` target so
# CI can build all of them without naming each one.
add_custom_target(fuzzers)

function(dragonperch_add_fuzzer name)
    cmake_parse_arguments(FUZZ "" "" "SOURCES;LIBRARIES;INCLUDES" ${ARGN})

    add_executable(dragonperch_fuzz_${name} ${name}_fuzzer.cpp ${FUZZ_SOURCES})

    target_link_libraries(dragonperch_fuzz_${name}
        PRIVATE DragonPerch::core DragonPerch::options ${FUZZ_LIBRARIES})

    if(FUZZ_INCLUDES)
        target_include_directories(dragonperch_fuzz_${name} PRIVATE ${FUZZ_INCLUDES})
    endif()

    # libFuzzer's main, and its coverage runtime. The instrumentation itself is already on
    # every object in the tree from the fuzzer-no-link in cmake/CompilerOptions.cmake.
    target_link_options(dragonperch_fuzz_${name} PRIVATE -fsanitize=fuzzer)

    add_dependencies(fuzzers dragonperch_fuzz_${name})

    # The seed corpus beside the binary, so a run is `dragonperch_fuzz_<name> corpus/<name>`
    # from the build directory with nothing else to remember.
    add_custom_command(TARGET dragonperch_fuzz_${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name}"
        "$<TARGET_FILE_DIR:dragonperch_fuzz_${name}>/corpus/${name}")
endfunction()

# Core parsers. No platform anything, so these build wherever the core does.
dragonperch_add_fuzzer(ini)
dragonperch_add_fuzzer(settings)
dragonperch_add_fuzzer(sprite_pack)
dragonperch_add_fuzzer(language)

# And the core's own behaviour, which is a different kind of target: the input has already
# been through a boundary, so what these look for is a wrong answer rather than a crash.
dragonperch_add_fuzzer(world)
dragonperch_add_fuzzer(simulation)

# The KWin report, which is Linux-only because the parsing lives in the Wayland head.
#
# Compiled from source rather than linked against the daemon: dragonperch-wl is an
# executable and has no library to link to, and pulling in only what kwin_geometry.cpp
# needs keeps the fuzzer's own surface small.
if(UNIX AND NOT APPLE AND TARGET dragonperch-wl)
    dragonperch_add_fuzzer(kwin_report
        SOURCES
            ${CMAKE_SOURCE_DIR}/src/linux/kwin_geometry.cpp
            ${CMAKE_SOURCE_DIR}/src/linux/session_bus.cpp
            ${CMAKE_SOURCE_DIR}/src/linux/log.cpp
        INCLUDES
            ${CMAKE_SOURCE_DIR}/src/linux
        LIBRARIES
            PkgConfig::SDBUS)

    # The atlas image. Most of what this exercises is libpng, which is not the point --
    # see fuzz/png_fuzzer.cpp for what is.
    dragonperch_add_fuzzer(png
        SOURCES
            ${CMAKE_SOURCE_DIR}/src/linux/png.cpp
        INCLUDES
            ${CMAKE_SOURCE_DIR}/src/linux
        LIBRARIES
            PkgConfig::PNG)
endif()
