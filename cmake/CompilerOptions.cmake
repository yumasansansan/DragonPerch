# SPDX-License-Identifier: GPL-3.0-or-later
#
# Every compiler and linker flag in the project lives here. Nothing else in the tree sets
# one.
#
# They divide into two scopes, and which scope a flag belongs in is a real decision:
#
#
#   optimisation  -> add_compile_options / add_link_options, so it reaches every target
#                    built in this tree, third-party dependencies included
#   policy        -> the DragonPerch::options interface target, linked PRIVATE, so it
#                    applies to our code and to nothing else
#
# Warnings-as-errors and -fno-rtti are policy: imposing them on somebody else's source
# fails builds that have nothing wrong with them. Optimisation is not -- there is no reason
# for one target to be built slower than another because of where its source came from.
#
# ---------------------------------------------------------------------------------------
# Sanitizers
# ---------------------------------------------------------------------------------------
option(DRAGONPERCH_SANITIZE "Enable sanitizers (ASan, UBSan, Fuzzer) for bug detection" OFF)

if(DRAGONPERCH_SANITIZE)
    if(MSVC)
        add_compile_options(
            /fsanitize=address
            /Zi
            /sdl
        )
        add_link_options(
            LINKER:/DEBUG
        )
    else()
        set(DRAGONPERCH_SANITIZERS -fsanitize=address,undefined,fuzzer-no-link)

        add_compile_options(
            ${DRAGONPERCH_SANITIZERS}
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
        )
        add_link_options(${DRAGONPERCH_SANITIZERS})

        # Checked here rather than discovered at the last link.
        #
        # The sanitizer runtimes are a separate package on Debian and Ubuntu --
        # libclang-rt-NN-dev -- and clang-NN does not depend on it. A machine with only
        # clang and lld compiles every file happily and then fails linking the one
        # executable with three lines of
        #
        #     ld.lld: error: cannot open .../libclang_rt.asan.a: No such file or directory
        #
        # which names a path nobody has and no package at all. Doing the same thing at
        # configure time costs one try_compile and turns that into a sentence.
        include(CheckCXXSourceCompiles)

        set(CMAKE_REQUIRED_FLAGS ${DRAGONPERCH_SANITIZERS})
        set(CMAKE_REQUIRED_LINK_OPTIONS ${DRAGONPERCH_SANITIZERS})
        check_cxx_source_compiles("int main() { return 0; }" DRAGONPERCH_SANITIZERS_LINK)
        unset(CMAKE_REQUIRED_FLAGS)
        unset(CMAKE_REQUIRED_LINK_OPTIONS)

        if(NOT DRAGONPERCH_SANITIZERS_LINK)
            string(REGEX MATCH "^[0-9]+" DRAGONPERCH_SAN_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")
            message(FATAL_ERROR
                "DRAGONPERCH_SANITIZE is on, but a program built with "
                "${DRAGONPERCH_SANITIZERS} does not link.\n\nThe usual cause is that the "
                "sanitizer runtimes are not installed: on Debian and Ubuntu they are in "
                "libclang-rt-${DRAGONPERCH_SAN_MAJOR}-dev, which clang-${DRAGONPERCH_SAN_MAJOR} "
                "does not depend on. Install it and configure again.")
        endif()
    endif()
endif()
#
# ---------------------------------------------------------------------------------------
# How flags reach the tools
# ---------------------------------------------------------------------------------------
#
#   target_compile_options -> cl.exe   / clang++     (compiler)
#   target_link_options    -> link.exe / clang++ -o  (linker)
#
# MSVC is worth spelling out, because the two tools take different flags and a linker flag
# handed to the compiler is silently ignored rather than rejected:
#
#   /O2 /Oi /Ot /Gy /Ob3 /Gw /GL    compiler  (cl.exe)
#   /LTCG /OPT:REF /OPT:ICF         linker    (link.exe)
#
# Multi-config generators -- Visual Studio, Ninja Multi-Config -- choose the configuration
# at build time, not at configure time. `if(CMAKE_BUILD_TYPE STREQUAL "Release")` is
# therefore meaningless here and would silently do nothing. Use generator expressions,
# $<$<CONFIG:Release>:...>, which are evaluated per configuration.
#
# One expression per flag. Wrapping several in a single $<$<CONFIG:Release>: ... > spanning
# multiple lines happens to work, but relies on how CMake splits unquoted arguments; one
# flag per expression cannot be misread.

# Link this PRIVATE, always. It carries our build policy -- warnings as errors, no RTTI,
# optimisation choices -- and policy is not a usage requirement: code that merely links us
# should not inherit it. Linking it PUBLIC from the core once meant the test binary
# inherited -Werror and then failed on Catch2's use of __COUNTER__, which Clang 22
# diagnoses as a C2y extension. Requirements that genuinely must propagate, such as the
# language standard, belong on the consuming target as PUBLIC compile features.
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
        /analyze
        /options:strict         # reject unknown compiler options instead of ignoring them
        /permissive-            # conformance mode; without it MSVC accepts non-standard code
        /utf-8                  # source and execution charset, or Japanese literals break
        /Zc:__cplusplus         # otherwise __cplusplus reports 199711L regardless of /std
        /Zc:preprocessor        # conforming preprocessor
        /Zc:inline              # drop unreferenced COMDATs
        /EHsc)                  # exceptions from C++ only; extern "C" is assumed nothrow
    target_link_options(dragonperch_options INTERFACE
        LINKER:/WX)
else()
    target_compile_options(dragonperch_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wunused -Werror)
endif()

# ---------------------------------------------------------------------------------------
# Security hardening
# ---------------------------------------------------------------------------------------
if(MSVC)
    add_compile_options(
        /guard:cf
        /guard:ehcont
    )
    add_link_options(
        LINKER:/CETCOMPAT
        LINKER:/GUARD:CF
        LINKER:/DYNAMICBASE
        LINKER:/NXCOMPAT
        LINKER:/HIGHENTROPYVA
    )
else()
    include(CheckCXXCompilerFlag)

    # -fcf-protection is x86 only and is an error elsewhere, so it is asked for rather than
    # assumed. Everything below it is portable.
    check_cxx_compiler_flag(-fcf-protection=full DRAGONPERCH_HAS_CF_PROTECTION)
    if(DRAGONPERCH_HAS_CF_PROTECTION)
        add_compile_options(-fcf-protection=full)
    endif()

    add_compile_options(
        -fstack-protector-strong
        -fstack-clash-protection
        -D_GLIBCXX_ASSERTIONS
    )

    # _FORTIFY_SOURCE needs care in two directions.
    #
    # It is undefined first because several distributions predefine it in their compiler
    # packages; redefining it to a different value is a macro-redefinition warning, and
    # -Werror turns that into a failed build on somebody else's machine rather than ours.
    #
    # And it is left out entirely under a sanitizer. The fortified string and memory
    # functions and ASan's interceptors are two implementations of the same checks, and
    # running both produces warnings at best and misattributed reports at worst. The
    # sanitizer is the better check of the two; when it is on it should be the only one.
    if(NOT DRAGONPERCH_SANITIZE)
        add_compile_options(
            $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
            $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
    endif()

    add_link_options(
        LINKER:-z,relro
        LINKER:-z,now
        LINKER:-z,noexecstack
    )

    # LLD, of the same version as the compiler that was chosen.
    #
    # Not `-fuse-ld=lld`, for the reason cmake/ClangLatest.cmake picks a numbered clang++:
    # on Ubuntu the unsuffixed name is the distribution's default and the default lags, so
    # asking for it pairs the newest compiler with an older linker. Without LTO that is
    # merely untidy. Release builds are ThinLTO, and the bitcode a compiler emits is only
    # guaranteed to be readable by its own version's linker -- so the versions have to
    # agree, and the way to make them agree is to derive one from the other rather than to
    # write both down.
    #
    # Missing is a hard error rather than a fall back to GNU ld. LLD is a build dependency;
    # a machine without it should be told so here, by name, instead of finding out from a
    # link failure or from a Release build that quietly stopped being LTO.
    string(REGEX MATCH "^[0-9]+" DRAGONPERCH_CLANG_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")

    find_program(DRAGONPERCH_LLD NAMES ld.lld-${DRAGONPERCH_CLANG_MAJOR})
    if(NOT DRAGONPERCH_LLD)
        message(FATAL_ERROR
            "ld.lld-${DRAGONPERCH_CLANG_MAJOR} was not found, and the compiler is Clang "
            "${CMAKE_CXX_COMPILER_VERSION}. DragonPerch links with LLD of the compiler's own "
            "version and does not fall back to GNU ld; install lld-${DRAGONPERCH_CLANG_MAJOR} "
            "and configure again.")
    endif()

    add_link_options(-fuse-ld=lld-${DRAGONPERCH_CLANG_MAJOR})
    message(STATUS "Linker: ${DRAGONPERCH_LLD}")
endif()

# ---------------------------------------------------------------------------------------
# Optimisation
# ---------------------------------------------------------------------------------------
# This is the block to extend. Release-only flags go inside $<$<CONFIG:Release>:...>;
# anything outside applies to Debug too, which is almost never wanted.
#
# Applied with add_compile_options rather than to the interface target, so it reaches
# *everything built in this tree*, third-party dependencies included -- Catch2 arrives
# through FetchContent, which is an add_subdirectory underneath tests/, and directory
# options are inherited. There is no reason for one target in a build to be optimised
# differently from another just because of where its source came from.
#
# Warnings and conformance stay on the interface target above, and that distinction is
# deliberate: -Werror on somebody else's code fails a build that has nothing wrong with it,
# which is exactly what Clang 22 diagnosing Catch2's __COUNTER__ already demonstrated.
# /GR- and -fno-rtti stay there too. They read as optimisations but they remove a language
# feature, and a third-party library is entitled to use typeid.
if(MSVC)
    # CMake's own Release flags are "/O2 /Ob2 /DNDEBUG". /Ob3 below is a stronger form of
    # /Ob2, and cl warns (D9025) when it is handed both -- a command-line warning, which
    # /WX does not turn into an error, so it would just sit in the log forever. Drop the
    # default so ours is the only inlining flag. /O2 is repeated harmlessly and left
    # explicit, because this file is meant to show the whole optimisation set in one place.
    string(REPLACE "/Ob2" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

    add_compile_options(
        $<$<CONFIG:Release>:/O2>    # maximise speed
        $<$<CONFIG:Release>:/Oi>    # emit intrinsics
        $<$<CONFIG:Release>:/Ot>    # favour fast code over small code
        $<$<CONFIG:Release>:/Gy>    # function-level linking, so /OPT:ICF has something to fold
        $<$<CONFIG:Release>:/Ob3>   # inline anything the compiler judges worthwhile, not just __inline
        $<$<CONFIG:Release>:/Gw>)   # each global into its own COMDAT, so /OPT:REF can drop unused data

    add_link_options(
        $<$<CONFIG:Release>:LINKER:/OPT:REF>         # discard unreferenced functions and data
        $<$<CONFIG:Release>:LINKER:/OPT:ICF>         # fold identical COMDATs
        $<$<CONFIG:Release>:LINKER:/INCREMENTAL:NO>) # incremental linking defeats both of the above

    # Ours only: removes a language feature rather than merely optimising, so it is not
    # something to impose on a dependency.
    target_compile_options(dragonperch_options INTERFACE
        $<$<CONFIG:Release>:/GR->)  # no RTTI; nothing here uses dynamic_cast or typeid
else()
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        # Paired with --gc-sections below: the compiler has to put each function and object
        # in its own section before the linker can drop the unused ones.
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>)

    add_link_options(
        $<$<CONFIG:Release>:LINKER:--gc-sections>)

    # Ours only, for the same reason as /GR- above.
    target_compile_options(dragonperch_options INTERFACE
        $<$<CONFIG:Release>:-fno-rtti>)
endif()

# ---------------------------------------------------------------------------------------
# Link-time optimisation
# ---------------------------------------------------------------------------------------
# The odd one out: not a flag on the interface target above, but a global CMake setting.
# Three reasons, in increasing order of how much they matter.
#
# 1. It could not go on the interface target even if that were tidier.
#    INTERPROCEDURAL_OPTIMIZATION is a *target property*, and target properties do not
#    travel through target_link_libraries the way compile and link options do. Only the
#    consuming target can carry it.
#
# 2. One line covers both toolchains. It expands to /GL plus /LTCG on MSVC and to -flto on
#    Clang and GCC, so the if(MSVC)/else() split above does not grow a third arm whose two
#    halves have to be kept in agreement by hand.
#
# 3. LTO is not only a compiler and linker concern -- it changes the archiver too, and this
#    project builds a static library. Writing $<$<CONFIG:Release>:/GL> and
#    $<$<CONFIG:Release>:/LTCG> by hand does work on MSVC: it was measured, and produced a
#    binary 512 bytes from the one this produces, with no warnings either way. But
#    target_link_options does not reach lib.exe for a static library, and on Clang or GCC
#    the same gap is worse -- -flto objects placed in an archive by plain `ar` give
#    "archive has no index" or undefined symbols at link, because the LTO plugin is never
#    loaded. CMake covers that by switching CMAKE_AR and CMAKE_RANLIB to the LLVM or GCC
#    wrappers when IPO is on.
#
#    The MSVC half of point 3 was verified on this machine. The Unix half is CMake's
#    documented behaviour and has not been exercised yet: there is no Linux target to build
#    until milestone 6.
#
# Checked rather than assumed, so an unsupported toolchain says so at configure time
# instead of failing at link with something obscure.
include(CheckIPOSupported)
check_ipo_supported(RESULT DRAGONPERCH_IPO_SUPPORTED OUTPUT DRAGONPERCH_IPO_ERROR)

if(DRAGONPERCH_SANITIZE)
    message(STATUS "Link-time optimisation: disabled (incompatible with sanitizers)")
elseif(DRAGONPERCH_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    message(STATUS "Link-time optimisation: enabled for Release")
else()
    # Fatal, and it used to be a STATUS line. That was wrong in the way this project keeps
    # finding things wrong: a Release build that quietly stopped being LTO is a different
    # binary from the one meant to ship, and nothing downstream notices. CI carried on,
    # built the packages, uploaded them and would have published them, only slower and
    # larger, with the reason a hundred lines up the configure log.
    #
    # If a machine genuinely cannot do LTO, that is worth saying out loud once rather than
    # discovering from a size regression later.
    set(DRAGONPERCH_IPO_HINT "")
    if(NOT MSVC AND (NOT CMAKE_CXX_COMPILER_AR OR CMAKE_CXX_COMPILER_AR MATCHES "NOTFOUND"))
        # The failure that prompted all this, and it is not obvious from the log: the test
        # dies building a static library, because CMake looks for llvm-ar of the compiler's
        # own version and does not find it. On Debian and Ubuntu that lives in llvm-NN,
        # which clang-NN does not depend on -- clang pulls llvm-NN-linker-tools and stops
        # there -- so installing only clang and lld leaves LTO unavailable.
        string(REGEX MATCH "^[0-9]+" DRAGONPERCH_IPO_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")
        set(DRAGONPERCH_IPO_HINT
            "\n\nCMAKE_CXX_COMPILER_AR is ${CMAKE_CXX_COMPILER_AR}, which is almost "
            "certainly the whole of it: ThinLTO needs llvm-ar and llvm-ranlib of the "
            "compiler's own version. On Debian and Ubuntu those are in the "
            "llvm-${DRAGONPERCH_IPO_MAJOR} package, which clang-${DRAGONPERCH_IPO_MAJOR} "
            "does not depend on. Install it and configure again.")
    endif()

    message(FATAL_ERROR
        "Link-time optimisation is not available, and Release builds require it."
        ${DRAGONPERCH_IPO_HINT}
        "\n\nWhat the check reported:\n${DRAGONPERCH_IPO_ERROR}")
endif()
