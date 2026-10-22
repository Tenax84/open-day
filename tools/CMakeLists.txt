add_executable(dbitool dbitool/main.cpp)
target_link_libraries(dbitool PRIVATE openday_formats)

add_executable(fftool fftool/main.cpp)
target_link_libraries(fftool PRIVATE openday_formats)
