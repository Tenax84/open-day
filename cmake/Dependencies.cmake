# Third-party dependencies, pinned by version and hash. They are fetched at
# configure time unless an installed package is found first.

include(FetchContent)

# SQLite (public domain): the engine reads the databases that
# tools/mdb/mdb2sqlite.py converts from the game's Jet files.
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    FetchContent_Declare(sqlite_amalgamation
        URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
        URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(sqlite_amalgamation)
    add_library(openday_sqlite3 STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
    target_include_directories(openday_sqlite3 SYSTEM PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
    target_compile_definitions(openday_sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=0)
    add_library(SQLite::SQLite3 ALIAS openday_sqlite3)
endif()

# SDL3 (zlib license): windowing, input and presentation.
if(OPENDAY_BUILD_VIEWER)
    find_package(SDL3 CONFIG QUIET)
    if(NOT SDL3_FOUND)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3
            URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.16.tar.gz
            URL_HASH SHA256=c2ee715e42ec520c4d11fd8d249ef1d2b2baf4ad31148b72b3b000276c0b3633
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(SDL3)
    endif()
endif()

# stb_truetype (public domain / MIT): rasterizes the user-interface font.
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/31c1ad37456438565541f4919958214b6e762fb4.tar.gz
    URL_HASH SHA256=e4e3bba9c572a4a4148373a914d88ea0f0d11de8cc2c66739926e7eca0223319
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(stb)

# xBRZ 1.9 (GPL-3.0): enlarges the interface art for the HD interface
# (docs/mechanics/interface.md, "HD interface").
FetchContent_Declare(xbrz
    URL https://downloads.sourceforge.net/project/xbrz/xBRZ/xBRZ_1.9.zip
    URL_HASH SHA256=b2dff73b3abd24a18a7cde78d5ff5ed8f0922296dce6ed734dce2264cd0a0fc9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(xbrz)

# Liberation Sans (SIL Open Font License 1.1): metric-compatible substitute
# for the Arial text the original draws with Windows GDI
# (docs/mechanics/interface.md). Embedded into the program.
FetchContent_Declare(liberation_fonts
    URL https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz
    URL_HASH SHA256=7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(liberation_fonts)
