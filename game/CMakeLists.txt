add_library(openday_game
    anim/animator.cpp
    anim/battle_animations.cpp
    anim/unit_animation.cpp
    map/terrain_map.cpp
    map/map_objects.cpp
    map/map_renderer.cpp
    map/rules.cpp
)
target_include_directories(openday_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openday_game PUBLIC openday_data openday_render PRIVATE openday_warnings)
