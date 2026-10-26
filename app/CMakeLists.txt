add_executable(openday
    app.cpp
    setup.cpp
    setup_ui.cpp
    battle_screen.cpp
    campaign_army.cpp
    campaign_city.cpp
    campaign_continent.cpp
    campaign_flow.cpp
    console.cpp
    debug_tools.cpp
    encyclopedia_screen.cpp
    game.cpp
    gog_download.cpp
    hd_interface.cpp
    hd_pack.cpp
    main.cpp
    map_screen.cpp
    mcp_server.cpp
    menus.cpp
    openday_options.cpp
    sharp_text.cpp
    ui_frames.cpp
    updater.cpp
    view_layout.cpp
    window_icon.cpp
)
target_include_directories(openday PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(openday PRIVATE OPENDAY_VERSION="${PROJECT_VERSION}")
target_link_libraries(openday PRIVATE openday_game openday_ui openday_platform openday_update openday_stb openday_xbrz SDL3::SDL3 openday_warnings)
if(WIN32)
    target_link_libraries(openday PRIVATE ws2_32) # the debug server (mcp_server.cpp)
    # GUI subsystem: no console window when started from the Start menu or
    # Explorer; from a terminal the output still appears (console.cpp).
    set_target_properties(openday PROPERTIES WIN32_EXECUTABLE ON)
    target_sources(openday PRIVATE openday.rc) # the program icon
endif()
if(NOT WIN32 AND NOT APPLE)
    # The window icon (window_icon.cpp), embedded (cmake/EmbedFile.cmake).
    set(icon_png ${PROJECT_SOURCE_DIR}/assets/icon/openday-256.png)
    set(icon_cpp ${CMAKE_CURRENT_BINARY_DIR}/embedded/windowIcon.cpp)
    add_custom_command(OUTPUT ${icon_cpp}
        COMMAND ${CMAKE_COMMAND} -DINPUT=${icon_png} -DOUTPUT=${icon_cpp} -DSYMBOL=windowIcon
                -P ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
        DEPENDS ${icon_png} ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
        COMMENT "Embedding openday-256.png")
    target_sources(openday PRIVATE ${icon_cpp})
    target_compile_definitions(openday PRIVATE OPENDAY_WINDOW_ICON)
endif()
if(APPLE)
    # OpenDay.app; neutral name and the project's own icon (no branding of
    # the original game).
    set(icon_icns ${PROJECT_SOURCE_DIR}/assets/icon/openday.icns)
    set_source_files_properties(${icon_icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(openday PRIVATE ${icon_icns})
    set_target_properties(openday PROPERTIES
        MACOSX_BUNDLE_ICON_FILE openday.icns
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME OpenDay
        MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/cmake/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME OpenDay
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.tenax84.openday
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_COPYRIGHT "GPL-3.0-or-later, OpenDay contributors")
endif()
