add_library(openday_game
    map/terrain_map.cpp
    map/map_renderer.cpp
)
target_include_directories(openday_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openday_game PUBLIC openday_data openday_render PRIVATE openday_warnings)
