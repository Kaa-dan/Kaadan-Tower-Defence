# Fails if anything under game/ includes a platform or graphics header.
#
# This is the single mechanical check that keeps the port cost low. Without it,
# a stray #include <windows.h> or SDL header creeps in during a late-night
# session and is only discovered months later when the Windows build starts.

file(GLOB_RECURSE GAME_SOURCES "${GAME_DIR}/*.h" "${GAME_DIR}/*.cpp")

set(FORBIDDEN
    "SDL"
    "windows.h"
    "GL/gl"
    "OpenGL/"
    "Cocoa/"
    "X11/"
    "vulkan"
    "d3d1"
)

set(VIOLATIONS "")
foreach(SOURCE ${GAME_SOURCES})
    file(READ "${SOURCE}" CONTENT)
    string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]" INCLUDES "${CONTENT}")
    foreach(INCLUDE ${INCLUDES})
        foreach(BAD ${FORBIDDEN})
            string(FIND "${INCLUDE}" "${BAD}" FOUND)
            if(NOT FOUND EQUAL -1)
                list(APPEND VIOLATIONS "${SOURCE}: ${INCLUDE}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(VIOLATIONS)
    message(STATUS "Platform dependencies found in the portable game core:")
    foreach(VIOLATION ${VIOLATIONS})
        message(STATUS "  ${VIOLATION}")
    endforeach()
    message(FATAL_ERROR "game/ must stay free of platform and graphics headers.")
endif()

message(STATUS "game core is platform-clean (${CMAKE_MATCH_COUNT} sources checked)")
