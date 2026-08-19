add_executable(kaadanTD main.cpp GameView.cpp)
# ktd_engine already exposes ktd_game transitively.
target_link_libraries(kaadanTD PRIVATE ktd_engine)
target_include_directories(kaadanTD PRIVATE .)

if(MSVC)
    target_compile_options(kaadanTD PRIVATE /W4)
    set_target_properties(kaadanTD PROPERTIES WIN32_EXECUTABLE TRUE)
else()
    target_compile_options(kaadanTD PRIVATE -Wall -Wextra)
endif()
