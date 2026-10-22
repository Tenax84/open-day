add_executable(dbi_test dbi_test.cpp)
target_link_libraries(dbi_test PRIVATE openday_formats)
add_test(NAME dbi_test COMMAND dbi_test)

# Optional checks against a local game installation (never committed).
if(DEFINED ENV{OPENDAY_GAME_DIR})
    add_test(NAME dbi_game_files COMMAND dbi_test --game-dir "$ENV{OPENDAY_GAME_DIR}")
endif()
