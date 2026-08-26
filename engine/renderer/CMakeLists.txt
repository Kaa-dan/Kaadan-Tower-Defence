# The decision half of the renderer: atlas packing, sort keys, batch planning.
#
# Deliberately links NOTHING but ktd_game -- no SDL, no OpenGL, no header from
# backends/. That is what lets tests/ link it under KTD_TESTS_ONLY, so the
# packer and the batcher run inside the 5-second core preset instead of behind
# a 94-second SDL configure. See docs/decisions/0013.
#
# purity_render_core fails the build if that ever stops being true.

add_library(ktd_render_core STATIC
    Atlas.cpp
    Batch.cpp
    Renderer.cpp
)

# Includes are written engine-relative ("renderer/Atlas.h"), matching how
# backends/ and app/ already spell them.
target_include_directories(ktd_render_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(ktd_render_core PUBLIC cxx_std_20)
target_link_libraries(ktd_render_core PUBLIC ktd_game)

ktd_set_warnings(ktd_render_core)
