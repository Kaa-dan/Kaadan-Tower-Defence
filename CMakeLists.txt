cmake_minimum_required(VERSION 3.24)
project(kaadanTD LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(KTD_BUILD_TESTS "Build headless game-core tests" ON)

add_subdirectory(game)     # pure C++ simulation. No platform, no graphics.
add_subdirectory(engine)   # platform + renderer interfaces and their backends.
add_subdirectory(app)      # the executable that wires the two together.

if(KTD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
