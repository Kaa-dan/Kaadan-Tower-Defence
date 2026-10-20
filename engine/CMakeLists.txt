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
    # A commit SHA, not a tag: tags are mutable, and a shipped build has to be
    # reproducible years from now. This is release-3.4.14.
    GIT_TAG 147a8ee32dbf9ac02f3794964490687b6bbda1bc
    GIT_SHALLOW FALSE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(SDL3)

# The backends, and only the backends. renderer/ and image/ are separate
# targets added from the root CMakeLists so they build without SDL -- see
# engine/renderer/ and engine/image/. image/LoadTexture.cpp is the exception: it
# joins a Platform to a Renderer, so it is compiled here.
add_library(ktd_engine STATIC
    backends/sdl/SDLPlatform.cpp
    backends/gl/GLRenderer.cpp
    backends/gl/GLLoader.cpp
    image/LoadTexture.cpp
)

target_include_directories(ktd_engine PUBLIC .)

# The engine may see the game's public headers (the renderer draws the world),
# but never the reverse. ktd_render_core carries ktd_game transitively.
target_link_libraries(ktd_engine PUBLIC ktd_render_core ktd_image PRIVATE SDL3::SDL3-static)

if(APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(ktd_engine PRIVATE ${OPENGL_FRAMEWORK})
endif()

ktd_set_warnings(ktd_engine)
