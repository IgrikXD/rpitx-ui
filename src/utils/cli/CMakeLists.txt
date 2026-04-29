# ----------------------------------------------------------
# Shared CLI parsing utility library
# ----------------------------------------------------------
find_package(CLI11 CONFIG REQUIRED)
add_library(rpitx_cli_utils STATIC
    cli_common.cpp
    cli_validators.cpp
)
target_include_directories(rpitx_cli_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpitx_cli_utils PUBLIC CLI11::CLI11)
