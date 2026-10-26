# Install rules and CPack packages: the engine only, never game data.
#
#   macOS:   OpenDay-<version>-macos-arm64.dmg    disk image: OpenDay.app and an
#                                                 Applications link only; the tools are
#                                                 in Contents/MacOS, the docs in
#                                                 Contents/Resources of the bundle
#   Windows: OpenDay-<version>-windows-x64.exe    installer (NSIS): openday.exe +
#                                                 tools/ + docs, Start menu, uninstaller
#   Linux:   OpenDay-<version>-linux-x64.tar.gz   bin/ + share/doc/openday/
#
#   cmake --build build --config Release --target package   (or: cd build && cpack -C Release)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW) # normalized install() destinations
endif()

set(OPENDAY_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Version in the package file names (the release tag)")

set(openday_tools dbitool fftool installtool mdbtool)
if(OPENDAY_BUILD_VIEWER)
    list(APPEND openday_tools mapview)
endif()

if(APPLE AND OPENDAY_BUILD_VIEWER)
    # Everything inside the bundle, so that the disk image holds nothing but
    # the app to drag, and the updater (which replaces the bundle) updates
    # the tools too.
    install(TARGETS openday BUNDLE DESTINATION .)
    set(openday_doc_dir OpenDay.app/Contents/Resources)
    set(openday_tool_dir OpenDay.app/Contents/MacOS)
elseif(APPLE)
    set(openday_doc_dir .)
    set(openday_tool_dir tools)
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
if(EXISTS "${OPENDAY_WEBVIEW2_DIR}/LICENSE.txt" AND TARGET openday_platform)
    install(FILES ${OPENDAY_WEBVIEW2_DIR}/LICENSE.txt DESTINATION ${openday_doc_dir}/licenses RENAME WebView2.txt)
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
set(CPACK_STRIP_FILES ON)
if(APPLE AND OPENDAY_BUILD_VIEWER)
    # Stripping breaks the linker's ad-hoc signature; sign again before packing.
    set(CPACK_PRE_BUILD_SCRIPTS "${PROJECT_SOURCE_DIR}/cmake/MacAdhocSign.cmake")
endif()
if(APPLE)
    set(CPACK_GENERATOR DragNDrop)
    set(CPACK_DMG_VOLUME_NAME OpenDay)
    # No license agreement dialog when the image is opened (GPL needs none).
    set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)
elseif(WIN32)
    set(CPACK_GENERATOR NSIS)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY OpenDay)
    # Fixed key (not "OpenDay <version>"), so a new version replaces the old.
    set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY OpenDay)
    set(CPACK_NSIS_PACKAGE_NAME OpenDay)
    set(CPACK_NSIS_DISPLAY_NAME OpenDay)
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
    set(CPACK_NSIS_UNINSTALL_NAME Uninstall)
    set(CPACK_NSIS_INSTALLED_ICON_NAME openday.exe)
    set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/assets/icon/openday.ico")
    set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/assets/icon/openday.ico")
    set(CPACK_NSIS_URL_INFO_ABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_NSIS_HELP_LINK "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # Start menu shortcut; openday.exe is in the installation root, not bin/.
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY .)
    if(OPENDAY_BUILD_VIEWER)
        set(CPACK_PACKAGE_EXECUTABLES openday OpenDay)
        # "Run OpenDay" on the last page: the updater starts the installer
        # and quits (docs/updates.md).
        set(CPACK_NSIS_MUI_FINISHPAGE_RUN openday.exe)
    endif()
else()
    set(CPACK_GENERATOR TGZ)
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
endif()
include(CPack)
