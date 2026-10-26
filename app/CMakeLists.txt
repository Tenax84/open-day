add_executable(openday
    app.cpp
    setup.cpp
    battle_screen.cpp
    campaign_army.cpp
    campaign_city.cpp
    campaign_continent.cpp
    campaign_flow.cpp
    console.cpp
    debug_tools.cpp
    encyclopedia_screen.cpp
    game.cpp
    main.cpp
    map_screen.cpp
    mcp_server.cpp
    menus.cpp
    openday_options.cpp
    sharp_text.cpp
    ui_frames.cpp
    updater.cpp
    view_layout.cpp
)
target_include_directories(openday PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(openday PRIVATE OPENDAY_VERSION="${PROJECT_VERSION}")
target_link_libraries(openday PRIVATE openday_game openday_ui openday_platform openday_update SDL3::SDL3 openday_warnings)
if(WIN32)
    target_link_libraries(openday PRIVATE ws2_32) # the debug server (mcp_server.cpp)
    # GUI subsystem: no console window when started from the Start menu or
    # Explorer; from a terminal the output still appears (console.cpp).
    set_target_properties(openday PROPERTIES WIN32_EXECUTABLE ON)
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
