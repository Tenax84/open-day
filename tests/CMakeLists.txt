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

add_executable(animation_test animation_test.cpp)
target_link_libraries(animation_test PRIVATE openday_render openday_warnings)
add_test(NAME animation_test COMMAND animation_test)

add_executable(anim_test anim_test.cpp)
target_link_libraries(anim_test PRIVATE openday_game openday_warnings)
add_test(NAME anim_test COMMAND anim_test)

add_executable(battle_test battle_test.cpp)
target_link_libraries(battle_test PRIVATE openday_game openday_warnings)
add_test(NAME battle_test COMMAND battle_test)

add_executable(ui_test ui_test.cpp)
target_link_libraries(ui_test PRIVATE openday_ui openday_warnings)
add_test(NAME ui_test COMMAND ui_test)

add_executable(campaign_test campaign_test.cpp)
target_link_libraries(campaign_test PRIVATE openday_game openday_warnings)
add_test(NAME campaign_test COMMAND campaign_test)

if(DEFINED ENV{OPENDAY_DATA_DIR})
    add_test(NAME campaign_game_files COMMAND campaign_test --data "$ENV{OPENDAY_DATA_DIR}")
endif()

add_executable(zip_test zip_test.cpp)
target_link_libraries(zip_test PRIVATE openday_formats openday_warnings)
add_test(NAME zip_test COMMAND zip_test)

add_executable(update_test update_test.cpp)
target_link_libraries(update_test PRIVATE openday_update openday_warnings)
add_test(NAME update_test COMMAND update_test)

add_executable(mdb_test mdb_test.cpp)
target_link_libraries(mdb_test PRIVATE openday_data openday_warnings)
add_test(NAME mdb_test COMMAND mdb_test)

# Optional: convert the local installation and compare it with a conversion
# made by tools/mdb/mdb2sqlite.py (OPENDAY_DATA_DIR).
if(DEFINED ENV{OPENDAY_GAME_DIR} AND DEFINED ENV{OPENDAY_DATA_DIR})
    add_test(NAME mdb_game_files COMMAND mdb_test --game-dir "$ENV{OPENDAY_GAME_DIR}" --compare "$ENV{OPENDAY_DATA_DIR}")
endif()

add_executable(info_test info_test.cpp)
target_link_libraries(info_test PRIVATE openday_game openday_warnings)
add_test(NAME info_test COMMAND info_test)
if(DEFINED ENV{OPENDAY_DATA_DIR})
    add_test(NAME info_game_files COMMAND info_test --data "$ENV{OPENDAY_DATA_DIR}")
endif()

add_executable(save_test save_test.cpp)
target_link_libraries(save_test PRIVATE openday_game openday_warnings)
add_test(NAME save_test COMMAND save_test)

add_executable(lzma_test lzma_test.cpp)
target_link_libraries(lzma_test PRIVATE openday_formats openday_warnings)
add_test(NAME lzma_test COMMAND lzma_test)

add_executable(install_test install_test.cpp)
target_link_libraries(install_test PRIVATE openday_data openday_warnings)
add_test(NAME install_test COMMAND install_test)
