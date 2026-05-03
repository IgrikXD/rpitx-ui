/**
 * @file cli_parse_result.h
 * @brief CLI parse outcome shared across migrated rpitx-ui tools.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

namespace rpitx::cli {
    /**
     * @brief Outcome of command-line argument parsing for migrated rpitx-ui tools.
     *
     * Distinguishes a successful parse, a user-visible error, and a help
     * request so that the caller can exit with a proper status code (0 for
     * Help, non-zero for Error) without extra sentinels.
     */
    enum class ParseResult {
        Ok,     ///< Options successfully populated.
        Error,  ///< Invalid arguments; caller should exit with non-zero code.
        Help,   ///< User requested help (--help); caller should exit cleanly.
    };
}  // namespace rpitx::cli
