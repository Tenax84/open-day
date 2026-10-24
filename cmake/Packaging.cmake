# Install rules and CPack packages: the engine only, never game data.
#
#   macOS:   OpenDay-<version>-macos-arm64.zip    OpenDay.app + tools/ + docs
#   Windows: OpenDay-<version>-windows-x64.zip    openday.exe + tools/ + docs
#   Linux:   OpenDay-<version>-linux-x64.tar.gz   bin/ + share/doc/openday/
#
#   cmake --build build --config Release --target package   (or: cd build && cpack -C Release)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW) # normalized install() destinations
endif()

set(OPENDAY_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Version in the package file names (the release tag)")

set(openday_tools dbitool fftool mdbtool)
if(OPENDAY_BUILD_VIEWER)
    list(APPEND openday_tools mapview)
endif()

if(APPLE)
    set(openday_doc_dir .)
    set(openday_tool_dir tools)
    if(OPENDAY_BUILD_VIEWER)
        install(TARGETS openday BUNDLE DESTINATION .)
    endif()
elseif(WIN32)
    set(openday_doc_dir .)
    set(openday_tool_dir tools)
    if(OPENDAY_BUILD_VIEWER)
        install(TARGETS openday RUNTIME DESTINATION .)
    endif()
    # The Visual C++ runtime DLLs, so that no redistributable is needed.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    include(InstallRequiredSystemLibraries)
else()
    include(GNUInstallDirs)
    set(openday_doc_dir ${CMAKE_INSTALL_DOCDIR})
    set(openday_tool_dir ${CMAKE_INSTALL_BINDIR})
    if(OPENDAY_BUILD_VIEWER)
        install(TARGETS openday RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

install(TARGETS ${openday_tools} RUNTIME DESTINATION ${openday_tool_dir})
install(FILES README.md LICENSE THIRD_PARTY.md DESTINATION ${openday_doc_dir})
# Licenses of the components linked or embedded into the programs.
install(FILES ${liberation_fonts_SOURCE_DIR}/LICENSE DESTINATION ${openday_doc_dir}/licenses
        RENAME LiberationFonts-OFL-1.1.txt)
install(FILES ${stb_SOURCE_DIR}/LICENSE DESTINATION ${openday_doc_dir}/licenses RENAME stb.txt)
if(OPENDAY_BUILD_VIEWER AND DEFINED sdl3_SOURCE_DIR)
    install(FILES ${sdl3_SOURCE_DIR}/LICENSE.txt DESTINATION ${openday_doc_dir}/licenses RENAME SDL3.txt)
endif()

# Platform part of the package name.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" openday_arch)
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    string(REPLACE ";" "-" openday_arch "${CMAKE_OSX_ARCHITECTURES}")
elseif(openday_arch MATCHES "^(amd64|x86_64|x64)$")
    set(openday_arch x64)
elseif(openday_arch MATCHES "^(aarch64|arm64)$")
    set(openday_arch arm64)
endif()
if(APPLE)
    set(openday_os macos)
elseif(WIN32)
    set(openday_os windows)
else()
    set(openday_os linux)
endif()

set(CPACK_PACKAGE_NAME OpenDay)
set(CPACK_PACKAGE_VENDOR "OpenDay contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Open-source engine for a 1998 turn-based strategy game (game data not included)")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Tenax84/open-day")
set(CPACK_PACKAGE_VERSION "${OPENDAY_PACKAGE_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "OpenDay-${OPENDAY_PACKAGE_VERSION}-${openday_os}-${openday_arch}")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_STRIP_FILES ON)
if(WIN32 OR APPLE)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
