# Mechanical guards over game/. Run as three separate CTest entries so a failure
# names the rule that broke rather than just "purity failed".
#
# Invoke with -DGAME_DIR=<path> -DRULE=<platform|determinism|stdlib>.
#
# Without these, a stray #include <windows.h> or a std::pow creeps in during a
# late-night session and is only discovered months later -- when the Windows
# build breaks, or worse, when a player's replay desyncs.

if(NOT DEFINED GAME_DIR)
    message(FATAL_ERROR "check_purity.cmake needs -DGAME_DIR=<path>")
endif()
if(NOT DEFINED RULE)
    set(RULE "platform")
endif()

file(GLOB_RECURSE GAME_SOURCES "${GAME_DIR}/*.h" "${GAME_DIR}/*.cpp")
list(LENGTH GAME_SOURCES SOURCE_COUNT)

# Files exempt from a rule, matched as a substring of the path. Math.cpp is the
# one place allowed to include <cmath>: it wraps the handful of functions that
# are IEEE-exact and provides deterministic replacements for the ones that are
# not, so every other file can be held to "no direct libm".
set(CMATH_EXEMPT "game/src/Math.cpp" "game/include/game/Math.h")

if(RULE STREQUAL "platform")
    # Forbidden #include substrings, matched case-insensitively.
    set(BAD_INCLUDES
        "sdl" "windows.h" "gl/gl" "opengl/" "cocoa/" "x11/" "vulkan"
        "d3d1" "dxgi" "metal" "wayland" "jni.h" "android/"
    )
    set(BAD_TOKENS "")
    set(DESCRIPTION "platform and graphics headers")

elseif(RULE STREQUAL "determinism")
    # Anything whose result or ordering is allowed to vary between runs,
    # machines or standard-library implementations.
    set(BAD_INCLUDES
        "<random>" "<chrono>" "<thread>" "<mutex>" "<atomic>" "<future>"
        "<execution>" "<ctime>" "<time.h>"
    )
    # std::sort is banned in favour of std::stable_sort: libstdc++, libc++ and
    # MSVC use different introsort pivots, so equal elements land in different
    # orders on different platforms. That is a silent cross-platform desync.
    set(BAD_TOKENS
        "rand(" "srand(" "std::rand" "clock(" "long double" "std::sort"
        "-ffast-math"
    )
    set(DESCRIPTION "non-deterministic facilities")

elseif(RULE STREQUAL "stdlib")
    # game/ owns schemas and parsing, never file I/O -- that belongs to the
    # platform layer. Unordered containers are banned because their iteration
    # order is implementation-defined.
    set(BAD_INCLUDES
        "<fstream>" "<iostream>" "<filesystem>" "<unordered_map>" "<unordered_set>"
    )
    set(BAD_TOKENS "")
    set(DESCRIPTION "host I/O and order-unstable containers")

else()
    message(FATAL_ERROR "unknown RULE '${RULE}'")
endif()

set(VIOLATIONS "")

foreach(SOURCE ${GAME_SOURCES})
    file(READ "${SOURCE}" RAW_CONTENT)

    # Strip comments before scanning. Otherwise a doc comment that says "never
    # use rand()" trips the rule against rand() -- which is exactly what
    # happened the first time this ran.
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " CONTENT "${RAW_CONTENT}")
    string(REGEX REPLACE "//[^\n]*" " " CONTENT "${CONTENT}")

    # --- forbidden includes ---
    string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]" INCLUDES "${CONTENT}")
    foreach(INCLUDE ${INCLUDES})
        string(TOLOWER "${INCLUDE}" INCLUDE_LOWER)
        foreach(BAD ${BAD_INCLUDES})
            string(FIND "${INCLUDE_LOWER}" "${BAD}" FOUND)
            if(NOT FOUND EQUAL -1)
                list(APPEND VIOLATIONS "${SOURCE}: ${INCLUDE}")
            endif()
        endforeach()
    endforeach()

    # --- forbidden tokens ---
    foreach(BAD ${BAD_TOKENS})
        string(FIND "${CONTENT}" "${BAD}" FOUND)
        if(NOT FOUND EQUAL -1)
            list(APPEND VIOLATIONS "${SOURCE}: uses '${BAD}'")
        endif()
    endforeach()

    # --- <cmath> is allowed only in the audited math wrapper ---
    # Switched on in Phase 1, once game/Math.h existed and Types.h, Grid.h and
    # World.cpp had been routed through it. The point is that pow, sin, exp and
    # atan2 are not correctly rounded and differ between glibc, MSVC and Apple
    # libm, so the set of files that can call them has to stay at exactly one.
    if(RULE STREQUAL "determinism" AND ENFORCE_CMATH)
        set(IS_EXEMPT FALSE)
        foreach(EXEMPT ${CMATH_EXEMPT})
            string(FIND "${SOURCE}" "${EXEMPT}" FOUND)
            if(NOT FOUND EQUAL -1)
                set(IS_EXEMPT TRUE)
            endif()
        endforeach()
        if(NOT IS_EXEMPT)
            foreach(INCLUDE ${INCLUDES})
                string(TOLOWER "${INCLUDE}" INCLUDE_LOWER)
                string(FIND "${INCLUDE_LOWER}" "<cmath>" FOUND)
                if(NOT FOUND EQUAL -1)
                    list(APPEND VIOLATIONS
                        "${SOURCE}: ${INCLUDE} (only Math.h/Math.cpp may include <cmath>)")
                endif()
            endforeach()
        endif()
    endif()
endforeach()

if(VIOLATIONS)
    list(REMOVE_DUPLICATES VIOLATIONS)
    message(STATUS "Forbidden ${DESCRIPTION} in the portable game core:")
    foreach(VIOLATION ${VIOLATIONS})
        message(STATUS "  ${VIOLATION}")
    endforeach()
    message(FATAL_ERROR "game/ must stay free of ${DESCRIPTION}.")
endif()

message(STATUS "purity/${RULE}: clean (${SOURCE_COUNT} sources checked)")
