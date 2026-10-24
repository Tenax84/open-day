add_executable(dbitool dbitool/main.cpp)
target_link_libraries(dbitool PRIVATE openday_formats openday_warnings)

add_executable(fftool fftool/main.cpp)
target_link_libraries(fftool PRIVATE openday_formats openday_warnings)

add_executable(mdbtool mdbtool/main.cpp)
target_link_libraries(mdbtool PRIVATE openday_data openday_warnings)

if(OPENDAY_BUILD_VIEWER)
    add_executable(mapview mapview/main.cpp)
    target_link_libraries(mapview PRIVATE openday_game SDL3::SDL3 openday_warnings)
    if(WIN32)
        # Console subsystem keeps stdout/stderr visible; SDL handles the window.
        set_target_properties(mapview PROPERTIES WIN32_EXECUTABLE OFF)
    endif()
endif()
