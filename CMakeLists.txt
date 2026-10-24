cmake_minimum_required(VERSION 3.21)
project(OpenDay VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENDAY_BUILD_VIEWER "Build the SDL3 map viewer (fetches SDL3 if not installed)" ON)

# Warning flags for OpenDay's own targets only; third-party code keeps its own.
add_library(openday_warnings INTERFACE)
if(MSVC)
    target_compile_options(openday_warnings INTERFACE /W4 /permissive- /utf-8)
    # Portable C functions such as getenv stay; no MSVC-only *_s variants.
    target_compile_definitions(openday_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(openday_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

include(cmake/Dependencies.cmake)

add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(tools)
if(OPENDAY_BUILD_VIEWER)
    add_subdirectory(app)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)
