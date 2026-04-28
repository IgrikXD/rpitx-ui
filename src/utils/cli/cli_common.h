/**
 * @file cli_common.h
 * @brief Shared CLI11 helpers used by migrated rpitx-ui binaries.
 *
 * Provides a small, deliberately thin wrapper around CLI11 that centralizes
 * the parts of the CLI v2 contract that every migrated binary must enforce:
 *   - help output goes to stdout (CLI11 default -h / --help, both accepted),
 *   - parse errors go to stderr,
 *   - --freq is parsed via parseFrequencyHz so that decimal/scientific
 *     notation that resolves to integer Hz is accepted while fractional Hz
 *     and out-of-range values are rejected.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <CLI/CLI.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

#include "cli_parse_result.h"

namespace rpitx::cli {
    /**
     * @brief Parse a textual frequency value into integer Hz.
     *
     * Accepts integer decimal notation (100000000), scientific notation
     * (100e6), and decimal/scientific values whose mathematical result is
     * integer-valued in Hz (100.0e6, 100.5e6 -> 100500000). Fractional Hz
     * values (e.g. 100.5) are rejected because the internal carrier
     * frequency parameter is stored as integer Hz.
     *
     * Rejects empty input, non-finite values, values <= 0, values >= 2^64,
     * and values that do not resolve to an integer Hz quantity.
     *
     * @param text Textual frequency value from the command line.
     * @return Parsed value in Hz on success, std::nullopt on failure.
     */
    [[nodiscard]] std::optional<std::uint64_t> parseFrequencyHz(std::string_view text);

    /**
     * @brief Parse a configured CLI11 application and translate the outcome into a ParseResult.
     *
     * Catches CLI::CallForHelp and prints help to stdout (exit cleanly).
     * Catches CLI::ParseError and prints the diagnostic plus help to stderr
     * (exit non-zero). Any other exception is allowed to propagate so that
     * the harness can surface it as a fatal error.
     *
     * @param app  Configured CLI11 application.
     * @param argc argc as received by main.
     * @param argv argv as received by main.
     * @return ParseResult::Ok on success, ::Help if --help was requested,
     *         ::Error on parse failure.
     */
    [[nodiscard]] ParseResult parseCliApp(CLI::App& app, int argc, char* argv[]);

    /**
     * @brief Convert a captured frequency string into integer Hz.
     *
     * Convenience wrapper around parseFrequencyHz that emits a uniform
     * "[ERROR] Invalid --freq: '...'" diagnostic on failure and returns
     * ParseResult::Error so the caller can short-circuit cleanly.
     *
     * @param text  Textual frequency value captured by CLI11.
     * @param out   Output - assigned the parsed value on success.
     * @return ParseResult::Ok on success, ::Error on parse failure
     *         (diagnostic already printed to stderr).
     */
    [[nodiscard]] ParseResult assignFrequencyHz(std::string_view text, std::uint64_t& out);
}  // namespace rpitx::cli
