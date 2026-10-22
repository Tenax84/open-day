add_library(openday_formats
    formats/dbi.cpp
    formats/png_writer.cpp
)
target_include_directories(openday_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
