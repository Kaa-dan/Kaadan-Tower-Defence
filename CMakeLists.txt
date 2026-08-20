cmake_minimum_required(VERSION 3.24)
project(kaadanTD VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(KtdWarnings)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(KTD_BUILD_TESTS "Build headless game-core tests" ON)
option(KTD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# Builds the simulation and its tests without configuring engine/ or app/.
#
# The point is CI speed: the determinism and unit-test jobs need nothing but
# ktd_game, and skipping this subdirectory skips fetching and compiling SDL
# entirely -- minutes per job down to seconds.
option(KTD_TESTS_ONLY "Skip engine/ and app/; build only the simulation and tests" OFF)

add_subdirectory(game)     # pure C++ simulation. No platform, no graphics.

if(NOT KTD_TESTS_ONLY)
    add_subdirectory(engine)   # platform + renderer interfaces and their backends.
    add_subdirectory(app)      # the executable that wires the two together.
endif()

if(KTD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
