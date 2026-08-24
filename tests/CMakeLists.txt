# Tests link ONLY ktd_game. If a test ever needs SDL or OpenGL to run, that is
# a signal the simulation has grown a platform dependency it should not have.
set(KTD_TEST_SOURCES
    test_content.cpp
    test_grid.cpp
    test_hash.cpp
    test_math.cpp
    test_path.cpp
    test_pool.cpp
    test_rng.cpp
    test_spatial.cpp
    test_storage.cpp
    test_world.cpp
)

add_executable(ktd_tests test_main.cpp ${KTD_TEST_SOURCES})
target_link_libraries(ktd_tests PRIVATE ktd_game)
target_include_directories(ktd_tests PRIVATE .)
ktd_set_warnings(ktd_tests)

# One CTest entry per TEST(), scraped from the sources at configure time.
#
# Registering the whole binary as a single test would mean `ctest -R targeting`
# does nothing and a CI failure reports "ktd_tests failed" instead of naming the
# case. The names come from the same TEST() macro that registers them at
# runtime, and --filter fails loudly on a name it cannot find, so the two lists
# cannot silently drift apart.
foreach(TEST_SOURCE ${KTD_TEST_SOURCES})
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_SOURCE} TEST_LINES REGEX "^TEST\\(")
    foreach(TEST_LINE ${TEST_LINES})
        string(REGEX REPLACE "^TEST\\(([A-Za-z0-9_]+)\\).*$" "\\1" TEST_NAME "${TEST_LINE}")
        add_test(NAME ${TEST_NAME} COMMAND ktd_tests --filter ${TEST_NAME})
    endforeach()
endforeach()

# The cross-platform determinism trace. Links only ktd_game, so it builds in
# seconds on a CI image with no display and no SDL.
add_executable(ktd_hashdump tools/hashdump.cpp)
target_link_libraries(ktd_hashdump PRIVATE ktd_game)
target_include_directories(ktd_hashdump PRIVATE .)
ktd_set_warnings(ktd_hashdump)

# Where the committed reference trace lives, so determinism_debug_matches_release
# can find it without depending on the working directory ctest was invoked from.
target_compile_definitions(ktd_tests
    PRIVATE KTD_REFERENCE_TRACE="${CMAKE_CURRENT_SOURCE_DIR}/data/reference_trace.tsv")

# The shipped content directory. Tests load the real balance files rather than a
# fixture, so `ctest` also proves the content parses and describes a routable
# level -- a typo in a .ktd file fails the build instead of the game.
foreach(TARGET ktd_tests ktd_hashdump)
    target_compile_definitions(${TARGET} PRIVATE KTD_CONTENT_DIR="${CMAKE_SOURCE_DIR}/content")
endforeach()

# Portability and determinism guards over game/.
foreach(RULE platform determinism stdlib)
    # ENFORCE_CMATH is on as of Phase 1: game/Math.h now exists and is the only
    # file in game/ permitted to include <cmath>. Everything else routes through
    # it, so the transcendental libm functions cannot reappear by accident.
    add_test(NAME purity_${RULE}
        COMMAND ${CMAKE_COMMAND}
                -DGAME_DIR=${CMAKE_SOURCE_DIR}/game
                -DRULE=${RULE}
                -DENFORCE_CMATH=ON
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_purity.cmake)
endforeach()
