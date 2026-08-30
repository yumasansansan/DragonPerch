# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(dragonperch WIN32
    main.cpp
    desktop_scanner.cpp
    win_event_watcher.cpp
    overlay_window.cpp
    paths.cpp
    frame_clock.cpp
    fullscreen.cpp
    gpu_device.cpp
    output_surface.cpp
    png.cpp
    settings_file.cpp
    shell.cpp
    sprite_pack_loader.cpp
    control.cpp
    tray.cpp
    sprite_renderer.cpp
    console.cpp
    log.cpp
    dragonperch.rc)

target_link_libraries(dragonperch PRIVATE
    DragonPerch::core
    DragonPerch::options
    d3d11
    dxgi
    dcomp
    d2d1
    dwmapi
    user32
    shell32
    shcore       # GetDpiForMonitor
    comctl32
    windowscodecs
    ole32)       # WIC

target_include_directories(dragonperch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# On this target, not globally. These are Windows API settings and only this head touches
# the Windows API -- and leaking _UNICODE into everything else broke the test binary, which
# picked up a wmain entry point from Catch2 while the linker was looking for main.
target_compile_definitions(dragonperch PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX                # or windows.h steals std::min and std::max
    WIN32_LEAN_AND_MEAN)
target_compile_definitions(dragonperch PRIVATE DRAGONPERCH_VERSION="${DRAGONPERCH_FULL_VERSION}")

# See the root CMakeLists for why this is a switch rather than a check on NDEBUG. The
# self-test is a whole file, so it leaves the build rather than being compiled to nothing.
target_compile_definitions(dragonperch
    PRIVATE $<${DRAGONPERCH_DIAGNOSTICS_CONDITION}:DRAGONPERCH_DIAGNOSTICS>)
target_sources(dragonperch
    PRIVATE $<${DRAGONPERCH_DIAGNOSTICS_CONDITION}:${CMAKE_CURRENT_SOURCE_DIR}/self_test.cpp>)

if(DRAGONPERCH_SANITIZE AND MSVC)
    get_filename_component(MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(DRAGONPERCH_ASAN_RUNTIME "${MSVC_BIN_DIR}/clang_rt.asan_dynamic-x86_64.dll")

    # Checked rather than copied blind: when the runtime is not where the compiler is, the
    # copy fails with a path and no explanation, and the real answer -- that the ASan
    # component of Visual Studio is not installed -- is nowhere in the message.
    if(EXISTS "${DRAGONPERCH_ASAN_RUNTIME}")
        add_custom_command(TARGET dragonperch POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DRAGONPERCH_ASAN_RUNTIME}"
            "$<TARGET_FILE_DIR:dragonperch>")
    else()
        message(WARNING
            "DRAGONPERCH_SANITIZE is on but ${DRAGONPERCH_ASAN_RUNTIME} is missing. "
            "Install the 'C++ AddressSanitizer' component of Visual Studio, or put the "
            "runtime on PATH; without it dragonperch will not start.")
    endif()
endif()

# The manifest arrives through dragonperch.rc, so stop the linker generating one of its
# own and invoking mt.exe over it.
target_link_options(dragonperch PRIVATE /MANIFEST:NO)

# WIN32 above makes this a GUI subsystem binary, so it has no console of its own: a stray
# console window flashing on login is not acceptable for something that autostarts. The
# diagnostic modes borrow the launching shell's console instead, see console.cpp.
