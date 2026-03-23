cmake_minimum_required(VERSION 3.25)
project(rpitx-ui
    VERSION 1.3
    DESCRIPTION "Radio transmitter for Raspberry Pi"
    LANGUAGES C CXX ASM
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Common compile options (optimization and -g handled by CMAKE_BUILD_TYPE)
add_compile_options(
    -Wall
    -Wno-unused-variable
)

add_subdirectory(src)
