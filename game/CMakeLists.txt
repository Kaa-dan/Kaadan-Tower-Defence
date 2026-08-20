# The game simulation. Deliberately links NOTHING.
#
# This target must never depend on SDL, OpenGL, Cocoa, Win32, or any other
# platform or graphics library. That restriction is the whole portability
# strategy: everything in here compiles as-is for Windows, Linux, mobile and
# console, and only the engine/ backends get rewritten per platform.
#
# There is a guard test (tests/check_purity.cmake) that fails the build if a
# platform header sneaks in here, so this stays true under pressure.

add_library(ktd_game STATIC
    src/Grid.cpp
    src/Path.cpp
    src/World.cpp
)

target_include_directories(ktd_game PUBLIC include)
target_compile_features(ktd_game PUBLIC cxx_std_20)

ktd_set_warnings(ktd_game)
