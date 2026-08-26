cmake_minimum_required(VERSION 3.24)
project(kaadanTD VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(KtdWarnings)
include(KtdDeterminism)

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

# Checked after the presets and any -D overrides have been folded into the
# flags variables, so a fast-math flag cannot slip in from the command line.
ktd_deny_fast_math()

# Link the GCC runtimes statically on MinGW.
#
# A MinGW build links libstdc++-6.dll and libgcc_s_seh-1.dll by name, and
# Windows resolves them off PATH. Any machine with a second MinGW on it -- and
# every machine with Git for Windows has one, at
# C:\Program Files\Git\mingw64\bin -- can therefore hand the process a
# libstdc++ from a different toolchain than the one that compiled it. That is
# not a link error or a warning; it is a segfault somewhere inside the standard
# library, at whatever call first depends on a layout the two versions disagree
# about. It cost this project a green CI run: ktd_hashdump crashed constructing
# a std::ifstream, but only in Release, and only in the steps that run under
# Git Bash, because those are the ones with Git's mingw64 on PATH.
#
# Static runtimes remove the question. It also matters past CI: a shipped
# Windows build must not depend on which DLL a player happens to have.
#
# Set before add_subdirectory so every target inherits it.
if(MINGW)
    add_link_options(-static-libgcc -static-libstdc++)
endif()

add_subdirectory(game)              # pure C++ simulation. No platform, no graphics.
add_subdirectory(engine/renderer)   # atlas + batch planning. Pure too, so tests can link it.

if(NOT KTD_TESTS_ONLY)
    add_subdirectory(engine)   # platform + renderer backends. This is the one that needs SDL.
    add_subdirectory(app)      # the executable that wires the two together.
endif()

if(KTD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
