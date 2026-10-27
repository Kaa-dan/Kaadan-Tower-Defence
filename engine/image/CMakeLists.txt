# Image decoding: PNG bytes to RGBA8.
#
# The only target that compiles stb_image (ADR 0014). Like ktd_render_core it
# links nothing but ktd_game -- no SDL, no OpenGL -- so tests/ can reach it
# under KTD_TESTS_ONLY and the decoder runs inside the core preset.
#
# LoadTexture.cpp in this directory is NOT part of it: that file needs a
# Platform and a Renderer, so ktd_engine compiles it.

add_library(ktd_image STATIC
    Image.cpp
)

target_include_directories(ktd_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# SYSTEM, so the vendored header's warnings are not ours. Our own code in
# Image.cpp still gets the full set, -Werror included. PRIVATE because nothing
# outside Image.cpp has any business including stb_image.h.
target_include_directories(ktd_image SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_features(ktd_image PUBLIC cxx_std_20)
target_link_libraries(ktd_image PUBLIC ktd_game)

ktd_set_warnings(ktd_image)
