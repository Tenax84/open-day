add_library(openday_formats
    formats/dbi.cpp
    formats/ff.cpp
    formats/png_writer.cpp
    formats/smk.cpp
)
target_include_directories(openday_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
