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

# clang-tidy off for the duration, because CMAKE_CXX_CLANG_TIDY reaches every target
# created from here down -- Catch2's included. Catch2 ships its own .clang-tidy, so it was
# being analysed against somebody else's check list and printing a hundred lines about
# trailing commas and #ifdef in the middle of our build. Its code is not ours to fix.
set(DRAGONPERCH_TIDY_HERE "${CMAKE_CXX_CLANG_TIDY}")
unset(CMAKE_CXX_CLANG_TIDY)

FetchContent_MakeAvailable(Catch2)

set(CMAKE_CXX_CLANG_TIDY "${DRAGONPERCH_TIDY_HERE}")

add_executable(dragonperch_tests
    edge_builder_test.cpp
    settings_test.cpp
    fall_recovery_test.cpp
    language_test.cpp
    simulation_test.cpp
    sprite_pack_file_test.cpp
    sprites_test.cpp
    text_test.cpp
    world_test.cpp)

# Deliberately not DragonPerch::options. The project builds with warnings as errors and
# without RTTI, and neither belongs on a test binary: a third-party header tripping our
# warning policy fails a build that has nothing wrong with it. Clang 22 diagnosing Catch2's
# __COUNTER__ as a C2y extension is exactly that.
#
# This only works because dragonperch_core links the options target PRIVATE. It used to be
# PUBLIC, and the flags arrived here anyway.
target_link_libraries(dragonperch_tests PRIVATE
    DragonPerch::core
    Catch2::Catch2WithMain)

target_compile_features(dragonperch_tests PRIVATE cxx_std_23)

include(Catch)
catch_discover_tests(dragonperch_tests)

# MSVC Address Sanitizer uses a dynamic library that must sit next to the executable
# or in the PATH. Copying it here means tests can run from anywhere without hanging
# on a missing DLL dialog.
if(DRAGONPERCH_SANITIZE AND MSVC)
    get_filename_component(MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(DRAGONPERCH_ASAN_RUNTIME "${MSVC_BIN_DIR}/clang_rt.asan_dynamic-x86_64.dll")

    # Checked rather than copied blind: when the runtime is not where the compiler is, the
    # copy fails with a path and no explanation, and the real answer -- that the ASan
    # component of Visual Studio is not installed -- is nowhere in the message.
    if(EXISTS "${DRAGONPERCH_ASAN_RUNTIME}")
        add_custom_command(TARGET dragonperch_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DRAGONPERCH_ASAN_RUNTIME}"
            "$<TARGET_FILE_DIR:dragonperch_tests>")
    else()
        message(WARNING
            "DRAGONPERCH_SANITIZE is on but ${DRAGONPERCH_ASAN_RUNTIME} is missing. "
            "Install the 'C++ AddressSanitizer' component of Visual Studio, or put the "
            "runtime on PATH; without it dragonperch_tests will not start.")
    endif()
endif()
