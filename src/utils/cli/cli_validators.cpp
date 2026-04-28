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
    // validator_name is the short type tag rendered next to the option in help output;
    // validator_desc is used in error diagnostics and richer help displays.
    const CLI::Validator PositiveFiniteFloat{
        [](const std::string& text) -> std::string {
            double value{};
            const auto first{text.data()};
            const auto last{text.data() + text.size()};
            if (const auto [ptr, ec]{std::from_chars(first, last, value)}; ec != std::errc{} || ptr != last) {
                return "must be a numeric value";
            }
            if (std::isfinite(value) == false) {
                return "must be finite";
            }
            if (value <= 0.0) {
                return "must be > 0";
            }
            return {};
        },
        "Positive finite floating-point value", "POSITIVE_FLOAT"};

    const CLI::Validator FrequencyHz{
        [](const std::string& text) -> std::string {
            if (parseFrequencyHz(text) == std::nullopt) {
                return "must be a positive integer Hz value (decimal or scientific notation, e.g. 100000000 or 100e6)";
            }
            return {};
        },
        "Frequency in Hz", "FREQ_HZ"};
}  // namespace rpitx::cli::validators
