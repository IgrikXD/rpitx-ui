/**
 * @file cli_utils.h
 * @brief Command-line argument parsing utilities shared across rpitx-ui tools.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>

/**
 * @brief Parse a numeric CLI argument via std::from_chars.
 *
 * Strict parsing: the entire input must be consumed and no trailing characters
 * are permitted. Locale-independent and allocation-free (unlike std::stoi
 * / std::stof), and reports failure via std::optional instead of exceptions.
 *
 * @tparam T Target numeric type (integer or floating-point).
 * @param arg Input argument.
 * @return Parsed value on success, std::nullopt on error.
 */
template <typename T>
[[nodiscard]] std::optional<T> parseNumericArg(std::string_view arg) {
    T value{};
    const auto first{arg.data()};
    const auto last{arg.data() + arg.size()};
    if (const auto [ptr, ec]{std::from_chars(first, last, value)}; ec != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return value;
}
