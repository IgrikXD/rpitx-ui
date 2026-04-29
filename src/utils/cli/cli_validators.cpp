/**
 * @file cli_validators.cpp
 * @brief Implementations of the reusable CLI11 validators.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "cli_validators.h"

#include <charconv>
#include <cmath>
#include <string>
#include <system_error>

#include "cli_common.h"

namespace rpitx::cli::validators {
    // The Validator constructor takes a callback returning an empty string on
    // success and a non-empty diagnostic on failure. CLI11 prepends the option
    // name automatically when raising the resulting ParseError, so the message
    // here describes only the constraint that was violated.

    // CLI::Validator constructor arguments are (function, validator_desc, validator_name).
    // validator_desc is appended after the type tag in help output as ":<desc>";
    // validator_name is an internal identifier (not shown in help). Both are left
    // empty here so help reads cleanly as e.g. "--freq TEXT REQUIRED" instead of
    // "--freq TEXT:Frequency in Hz REQUIRED" - the option's own description
    // already explains the value, and the validator returns its own diagnostic
    // string on failure.
    const CLI::Validator PositiveFiniteFloat{
        [](const std::string& text) -> std::string {
            float value{};
            const auto first{text.data()};
            const auto last{first + text.size()};
            if (const auto [ptr, ec]{std::from_chars(first, last, value)};
                ptr != last || ec == std::errc::invalid_argument) {
                return "must be a numeric value";
            } else if (ec == std::errc::result_out_of_range || std::isfinite(value) == false || value <= 0.0F) {
                return "must be a positive finite float";
            }
            return {};
        },
        ""};

    const CLI::Validator FrequencyHz{
        [](const std::string& text) -> std::string {
            if (parseFrequencyHz(text) == std::nullopt) {
                return "must be a positive integer Hz value (decimal or scientific notation, e.g. 100000000 or 100e6)";
            }
            return {};
        },
        ""};
}  // namespace rpitx::cli::validators
