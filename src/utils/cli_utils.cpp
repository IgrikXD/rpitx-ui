/**
 * @file cli_utils.cpp
 * @brief Non-template implementations for cli_utils.h.
 *
 * Housing the iostream-dependent helpers here keeps <iostream> out of
 * cli_utils.h, so every translation unit that uses the parsing helpers
 * does not pay the compile-time cost of including it.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 16.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "cli_utils.h"

#include <iostream>

bool containsFlag(std::span<char* const> args, std::string_view flag) {
    return std::ranges::any_of(args, [flag](const char* arg) { return std::string_view{arg} == flag; });
}

ParseResult reportInvalidValue(std::string_view what, std::string_view value) {
    std::cerr << "[ERROR] Invalid " << what << ": '" << value << "'" << std::endl;
    return ParseResult::Error;
}
