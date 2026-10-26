add_library(openday_formats
    formats/bmp_palette.cpp
    formats/byte_source.cpp
    formats/dbi.cpp
    formats/deflate.cpp
    formats/ff.cpp
    formats/game_path.cpp
    formats/hash.cpp
    formats/inflate.cpp
    formats/inno_setup.cpp
    formats/iso9660.cpp
    formats/json.cpp
    formats/lzma.cpp
    formats/mdb.cpp
    formats/palcycle.cpp
    formats/pe_resources.cpp
    formats/png_writer.cpp
    formats/sha256.cpp
    formats/smk.cpp
    formats/tar.cpp
    formats/zip.cpp
)
target_include_directories(openday_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openday_formats PRIVATE openday_warnings)

# Release information of the updater (no network code; testable).
add_library(openday_update update/release.cpp)
target_include_directories(openday_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openday_update PUBLIC openday_formats PRIVATE openday_warnings)

add_library(openday_data data/game_source.cpp data/mdb_import.cpp data/sqlite_db.cpp)
target_include_directories(openday_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openday_data PUBLIC SQLite::SQLite3 openday_formats PRIVATE openday_warnings)

add_library(openday_stb STATIC render/stb_truetype_impl.cpp render/stb_vorbis_impl.c)
target_include_directories(openday_stb SYSTEM PUBLIC ${stb_SOURCE_DIR})

# The interface font, embedded (cmake/EmbedFile.cmake).
set(OPENDAY_FONT_DIR ${liberation_fonts_SOURCE_DIR})
set(embedded_sources)
foreach(pair "LiberationSans-Regular.ttf=fontRegular" "LiberationSans-Bold.ttf=fontBold")
    string(REPLACE "=" ";" parts "${pair}")
    list(GET parts 0 file)
    list(GET parts 1 symbol)
    set(out ${CMAKE_CURRENT_BINARY_DIR}/embedded/${symbol}.cpp)
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -DINPUT=${OPENDAY_FONT_DIR}/${file} -DOUTPUT=${out} -DSYMBOL=${symbol}
                -P ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
        DEPENDS ${OPENDAY_FONT_DIR}/${file} ${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake
        COMMENT "Embedding ${file}")
    list(APPEND embedded_sources ${out})
endforeach()
add_library(openday_embedded STATIC ${embedded_sources})
target_compile_features(openday_embedded PUBLIC cxx_std_20)

add_library(openday_render
    render/animation.cpp
    render/font.cpp
    render/indexed_surface.cpp
    render/smk_frames.cpp
    render/text_layer.cpp
)
target_include_directories(openday_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openday_render PUBLIC openday_formats PRIVATE openday_stb openday_embedded openday_warnings)

add_library(openday_ui
    ui/atlas.cpp
    ui/widgets.cpp
)
target_include_directories(openday_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openday_ui PUBLIC openday_render openday_formats PRIVATE openday_warnings)

# Windowing-system parts (SDL3), only with the viewer / game executables.
if(OPENDAY_BUILD_VIEWER)
    add_library(openday_platform platform/audio.cpp platform/http.cpp)
    target_include_directories(openday_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(openday_platform PUBLIC openday_formats SDL3::SDL3 PRIVATE openday_stb openday_warnings)
    # HTTPS: WinHTTP on Windows; elsewhere libcurl, loaded at run time.
    if(WIN32)
        target_link_libraries(openday_platform PRIVATE winhttp)
    else()
        target_link_libraries(openday_platform PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()
