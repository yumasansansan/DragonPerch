# SPDX-License-Identifier: GPL-3.0-or-later
#
# Picks the newest Clang on the machine.
#
# A toolchain file rather than something in CMakeLists.txt, because the compiler has to be
# chosen before project() and nothing after that point can change it.
#
# Why not simply `clang++`. On Ubuntu the unsuffixed name is the distribution's default,
# and the default lags: 26.04 ships Clang 21 under that name while llvm.org's packages put
# Clang 22 beside it as `clang++-22`. Asking for the plain name therefore asks for the
# *older* compiler, which is the opposite of what this project wants and is the sort of
# thing that only surfaces much later as a confusing diagnostic.
#
# Why not a fixed `clang++-22` either, which is what this used to be. It needs an edit
# every release, and a machine that has only the next version installed cannot configure at
# all -- so being current costs a commit, and being ahead is an error.
#
# Clang is a build dependency and there is deliberately no fall back to GCC. A machine
# without it should say so here, in one line, rather than half way through a build.

# The search is not free and a toolchain file is read several times during configure, so
# the answer is cached and the work happens once.
if(NOT DEFINED DRAGONPERCH_CLANG_CXX)

    # 19 is where the C++23 support this project relies on became usable; the top is just a
    # number far enough ahead that it will not need touching.
    set(_dp_oldest 19)
    set(_dp_newest 40)

    set(_dp_best_version 0)
    set(_dp_best_cxx "")
    set(_dp_best_c "")

    # Ascending, keeping the last that matched, so the highest wins.
    foreach(_dp_version RANGE ${_dp_oldest} ${_dp_newest})
        # Cleared first, and this is not belt and braces: find_program skips the search
        # entirely when its result variable already holds a hit. Reusing the variable
        # without unsetting it makes every iteration after the first "succeed" with the
        # first version's path, so a machine with 19 and 22 installed picks 19 and reports
        # it as the newest.
        unset(_dp_cxx)
        unset(_dp_c)

        find_program(_dp_cxx NAMES clang++-${_dp_version} NO_CACHE)
        find_program(_dp_c NAMES clang-${_dp_version} NO_CACHE)
        if(_dp_cxx AND _dp_c)
            set(_dp_best_version ${_dp_version})
            set(_dp_best_cxx "${_dp_cxx}")
            set(_dp_best_c "${_dp_c}")
        endif()
    endforeach()

    # And the unsuffixed pair, in case it is newer than every numbered one -- a build from
    # source, or a distribution that has caught up. Asked its version rather than assumed.
    unset(_dp_cxx)
    unset(_dp_c)
    find_program(_dp_cxx NAMES clang++ NO_CACHE)
    find_program(_dp_c NAMES clang NO_CACHE)
    if(_dp_cxx AND _dp_c)
        execute_process(COMMAND "${_dp_cxx}" --version
                        OUTPUT_VARIABLE _dp_banner
                        ERROR_QUIET
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_dp_banner MATCHES "clang version ([0-9]+)")
            if(CMAKE_MATCH_1 GREATER _dp_best_version)
                set(_dp_best_version ${CMAKE_MATCH_1})
                set(_dp_best_cxx "${_dp_cxx}")
                set(_dp_best_c "${_dp_c}")
            endif()
        endif()
    endif()

    if(_dp_best_version EQUAL 0)
        message(FATAL_ERROR
            "No Clang found. DragonPerch builds with Clang and LLD and does not fall back "
            "to GCC; install clang and lld (llvm.org's apt packages, or your distribution's "
            "clang-NN) and configure again.")
    endif()

    set(DRAGONPERCH_CLANG_CXX "${_dp_best_cxx}" CACHE FILEPATH "The newest clang++ found")
    set(DRAGONPERCH_CLANG_C "${_dp_best_c}" CACHE FILEPATH "The newest clang found")
    message(STATUS "Compiler: ${_dp_best_cxx} (Clang ${_dp_best_version})")
endif()

set(CMAKE_C_COMPILER "${DRAGONPERCH_CLANG_C}")
set(CMAKE_CXX_COMPILER "${DRAGONPERCH_CLANG_CXX}")
