include(FetchContent)

# SDL3 is vendored through FetchContent rather than a system package. That
# keeps the build reproducible and, more importantly, means the later Windows
# and Linux builds need no package manager at all -- CMake fetches and builds
# the same pinned source everywhere.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.14
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(SDL3)

add_library(ktd_engine STATIC
    renderer/Renderer.cpp
    backends/sdl/SDLPlatform.cpp
    backends/gl/GLRenderer.cpp
    backends/gl/GLLoader.cpp
)

target_include_directories(ktd_engine PUBLIC .)

# The engine may see the game's public headers (the renderer draws the world),
# but never the reverse.
target_link_libraries(ktd_engine PUBLIC ktd_game PRIVATE SDL3::SDL3-static)

# SDL owns the GL context, so the platform header is the only thing that needs
# to be public here.
target_link_libraries(ktd_engine PUBLIC SDL3::Headers)

if(APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(ktd_engine PRIVATE ${OPENGL_FRAMEWORK})
endif()

if(MSVC)
    target_compile_options(ktd_engine PRIVATE /W4)
else()
    target_compile_options(ktd_engine PRIVATE -Wall -Wextra)
endif()
