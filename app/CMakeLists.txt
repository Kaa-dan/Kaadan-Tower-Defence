add_executable(kaadanTD main.cpp GameView.cpp Content.cpp)
# ktd_engine already exposes ktd_game transitively.
target_link_libraries(kaadanTD PRIVATE ktd_engine)
target_include_directories(kaadanTD PRIVATE .)

# Where the balance files live.
#
# Pointing at the source tree means F5 reloads what the editor just saved, which
# is the entire point of hot reload during development. Packaging a shipped
# build reads them from next to the executable instead; that is Phase 9's job.
target_compile_definitions(kaadanTD PRIVATE KTD_CONTENT_DIR="${CMAKE_SOURCE_DIR}/content")

if(MSVC)
    set_target_properties(kaadanTD PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

ktd_set_warnings(kaadanTD)
