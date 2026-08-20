add_executable(kaadanTD main.cpp GameView.cpp)
# ktd_engine already exposes ktd_game transitively.
target_link_libraries(kaadanTD PRIVATE ktd_engine)
target_include_directories(kaadanTD PRIVATE .)

if(MSVC)
    set_target_properties(kaadanTD PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

ktd_set_warnings(kaadanTD)
