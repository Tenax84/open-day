add_executable(openday
    app.cpp
    setup.cpp
    battle_screen.cpp
    campaign_army.cpp
    campaign_city.cpp
    campaign_continent.cpp
    campaign_flow.cpp
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
if(APPLE)
    # OpenDay.app; neutral name and no icon (no branding of the original game).
    set_target_properties(openday PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME OpenDay
        MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/cmake/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME OpenDay
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.tenax84.openday
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT "GPL-3.0-or-later, OpenDay contributors")
endif()
