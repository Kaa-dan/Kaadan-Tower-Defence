# Tests link ONLY ktd_game. If a test ever needs SDL or OpenGL to run, that is
# a signal the simulation has grown a platform dependency it should not have.
set(KTD_TEST_SOURCES
    test_grid.cpp
    test_path.cpp
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

# Portability and determinism guards over game/.
foreach(RULE platform determinism stdlib)
    add_test(NAME purity_${RULE}
        COMMAND ${CMAKE_COMMAND}
                -DGAME_DIR=${CMAKE_SOURCE_DIR}/game
                -DRULE=${RULE}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_purity.cmake)
endforeach()
