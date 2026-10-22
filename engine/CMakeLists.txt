add_library(openday_formats
    formats/bmp_palette.cpp
    formats/dbi.cpp
    formats/ff.cpp
    formats/palcycle.cpp
    formats/png_writer.cpp
    formats/smk.cpp
)
target_include_directories(openday_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openday_formats PRIVATE openday_warnings)

add_library(openday_data data/sqlite_db.cpp)
target_include_directories(openday_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openday_data PUBLIC SQLite::SQLite3 PRIVATE openday_warnings)

add_library(openday_render render/indexed_surface.cpp)
target_include_directories(openday_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openday_render PUBLIC openday_formats PRIVATE openday_warnings)
