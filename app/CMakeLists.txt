add_executable(openday
    app.cpp
    battle_screen.cpp
    game.cpp
    main.cpp
    menus.cpp
    ui_frames.cpp
)
target_include_directories(openday PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(openday PRIVATE OPENDAY_VERSION="${PROJECT_VERSION}")
target_link_libraries(openday PRIVATE openday_game openday_ui openday_platform SDL3::SDL3 openday_warnings)
if(WIN32)
    # Console subsystem keeps stdout/stderr visible; SDL handles the window.
    set_target_properties(openday PROPERTIES WIN32_EXECUTABLE OFF)
endif()
