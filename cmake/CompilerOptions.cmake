# SPDX-License-Identifier: GPL-3.0-or-later
#
# Every compiler and linker flag in the project lives here, on one interface target that
# each real target links. Nothing else in the tree should call add_compile_options.
#
# ---------------------------------------------------------------------------------------
# How flags reach the tools
# ---------------------------------------------------------------------------------------
#
#   target_compile_options -> cl.exe   / clang++     (compiler)
#   target_link_options    -> link.exe / clang++ -o  (linker)
#
# MSVC is the case worth spelling out, because the two tools take different flags and a
# linker flag handed to the compiler is silently ignored rather than rejected:
#
#   /O2 /Oi /Ot /GL      compiler  (cl.exe)   -- optimisation, whole-program analysis
#   /LTCG /OPT:REF       linker    (link.exe) -- link-time code generation, dead stripping
#
# /GL and /LTCG are a pair: /GL makes cl emit IL instead of machine code, and /LTCG makes
# link generate the code. Using one without the other is either a warning or a silent loss
# of the optimisation. CMake pairs them for you through
# CMAKE_INTERPROCEDURAL_OPTIMIZATION, which is why LTO below is expressed that way rather
# than by writing the flags out.
#
# Multi-config generators (Visual Studio, Ninja Multi-Config) choose the configuration at
# build time, not at configure time. `if(CMAKE_BUILD_TYPE STREQUAL "Release")` is therefore
# meaningless here and would silently do nothing. Use generator expressions --
# $<$<CONFIG:Release>:...> -- which are evaluated per configuration.

add_library(dragonperch_options INTERFACE)
add_library(DragonPerch::options ALIAS dragonperch_options)

# ---------------------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------------------
# Stated as a compile feature rather than a raw -std= flag so each compiler gets the
# spelling it wants: MSVC needs /std:c++latest for C++23 today, Clang and GCC take
# -std=c++23. Anything that links this target inherits the requirement.
target_compile_features(dragonperch_options INTERFACE cxx_std_23)

# ---------------------------------------------------------------------------------------
# Warnings, diagnostics, language conformance
# ---------------------------------------------------------------------------------------
if(MSVC)
    target_compile_options(dragonperch_options INTERFACE
        /W4 /WX
        /permissive-            # conformance mode; without it MSVC accepts non-standard code
        /utf-8                  # source and execution charset, or Japanese literals break
        /Zc:__cplusplus         # otherwise __cplusplus reports 199711L regardless of /std
        /Zc:preprocessor        # conforming preprocessor
        /Zc:inline              # drop unreferenced COMDATs
        /EHsc)                  # exceptions from C++ only; extern "C" is assumed nothrow
else()
    target_compile_options(dragonperch_options INTERFACE
        -Wall -Wextra -Wpedantic -Werror)
endif()

# ---------------------------------------------------------------------------------------
# Optimisation
# ---------------------------------------------------------------------------------------
# This is the block to extend. Release-only flags go inside $<$<CONFIG:Release>:...>;
# anything outside it applies to Debug too, which is almost never what is wanted.
# One generator expression per flag. Wrapping several flags in a single
# $<$<CONFIG:Release>: ... > spanning multiple lines happens to work, but it relies on how
# CMake splits unquoted arguments; one flag per expression cannot be misread.
if(MSVC)
    target_compile_options(dragonperch_options INTERFACE
        $<$<CONFIG:Release>:/O2>    # maximise speed
        $<$<CONFIG:Release>:/Oi>    # emit intrinsics
        $<$<CONFIG:Release>:/Ot>    # favour fast code over small code
        $<$<CONFIG:Release>:/Gy>    # function-level linking, so /OPT:ICF has something to fold
        $<$<CONFIG:Release>:/GR->)  # no RTTI; nothing here uses dynamic_cast or typeid

    target_link_options(dragonperch_options INTERFACE
        $<$<CONFIG:Release>:/OPT:REF>        # discard unreferenced functions and data
        $<$<CONFIG:Release>:/OPT:ICF>        # fold identical COMDATs
        $<$<CONFIG:Release>:/INCREMENTAL:NO>)# incremental linking defeats both of the above
else()
    target_compile_options(dragonperch_options INTERFACE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-fno-rtti>
        # Paired with --gc-sections below: the compiler has to put each function and object
        # in its own section before the linker can drop the unused ones.
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>)

    target_link_options(dragonperch_options INTERFACE
        $<$<CONFIG:Release>:-Wl,--gc-sections>)
endif()

# ---------------------------------------------------------------------------------------
# Link-time optimisation
# ---------------------------------------------------------------------------------------
# Expressed through CMake rather than as raw flags so the compiler and linker halves stay
# paired: /GL with /LTCG on MSVC, -flto on Clang and GCC. Checked rather than assumed,
# because a toolchain that cannot do it fails at link time with a confusing message.
include(CheckIPOSupported)
check_ipo_supported(RESULT DRAGONPERCH_IPO_SUPPORTED OUTPUT DRAGONPERCH_IPO_ERROR)

if(DRAGONPERCH_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    message(STATUS "Link-time optimisation: enabled for Release")
else()
    message(STATUS "Link-time optimisation: unavailable (${DRAGONPERCH_IPO_ERROR})")
endif()
