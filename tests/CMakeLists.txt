# Tests link ONLY ktd_game. If a test ever needs SDL or OpenGL to run, that is
# a signal the simulation has grown a platform dependency it should not have.
add_executable(ktd_tests
    test_main.cpp
    test_grid.cpp
    test_path.cpp
    test_world.cpp
)
target_link_libraries(ktd_tests PRIVATE ktd_game)
target_include_directories(ktd_tests PRIVATE .)

add_test(NAME game_core COMMAND ktd_tests)

# Portability guard: fails if platform or graphics headers appear under game/.
add_test(NAME core_purity
    COMMAND ${CMAKE_COMMAND}
            -DGAME_DIR=${CMAKE_SOURCE_DIR}/game
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_purity.cmake)
