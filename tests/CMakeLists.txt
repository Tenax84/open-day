add_executable(dbi_test dbi_test.cpp)
target_link_libraries(dbi_test PRIVATE openday_formats openday_warnings)
add_test(NAME dbi_test COMMAND dbi_test)

# Optional checks against a local game installation (never committed).
if(DEFINED ENV{OPENDAY_GAME_DIR})
    add_test(NAME dbi_game_files COMMAND dbi_test --game-dir "$ENV{OPENDAY_GAME_DIR}")
endif()

add_executable(ff_test ff_test.cpp)
target_link_libraries(ff_test PRIVATE openday_formats openday_warnings)
add_test(NAME ff_test COMMAND ff_test)

if(DEFINED ENV{OPENDAY_GAME_DIR})
    add_test(NAME ff_game_files COMMAND ff_test --game-dir "$ENV{OPENDAY_GAME_DIR}")
endif()

add_executable(palcycle_test palcycle_test.cpp)
target_link_libraries(palcycle_test PRIVATE openday_formats openday_warnings)
add_test(NAME palcycle_test COMMAND palcycle_test)

if(DEFINED ENV{OPENDAY_GAME_DIR})
    add_test(NAME palcycle_game_files COMMAND palcycle_test --game-dir "$ENV{OPENDAY_GAME_DIR}")
endif()

add_executable(map_test map_test.cpp)
target_link_libraries(map_test PRIVATE openday_game openday_warnings)
add_test(NAME map_test COMMAND map_test)
