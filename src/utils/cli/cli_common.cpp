/**
 * @file cli_common.cpp
 * @brief Implementation of the CLI11 helpers shared by migrated binaries.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 28.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "cli_common.h"

#include <charconv>
#include <cmath>
#include <iostream>
#include <system_error>

namespace rpitx::cli {
    std::optional<std::uint64_t> parseFrequencyHz(std::string_view text) {
        if (text.empty()) {
            return std::nullopt;
        }

        // Parse via std::from_chars on double so that scientific and decimal
        // notation are both accepted in a locale-independent, allocation-free
        // way. The entire input must be consumed - trailing garbage is an error.
        double value{};
        const auto first{text.data()};
        const auto last{text.data() + text.size()};
        if (const auto [ptr, ec]{std::from_chars(first, last, value)}; ec != std::errc{} || ptr != last) {
            return std::nullopt;
        }

        if (std::isfinite(value) == false) {
            return std::nullopt;
        }
        if (value <= 0.0) {
            return std::nullopt;
        }
        // Guard the double -> uint64_t conversion. UINT64_MAX (2^64 - 1) is
        // not exactly representable as double; std::ldexp(1.0, 64) is exactly
        // 2^64 and is the strict upper bound any finite double can convert
        // safely from.
        if (value >= std::ldexp(1.0, 64)) {
            return std::nullopt;
        }

        // Reject fractional Hz: the internal carrier frequency parameter is
        // stored as integer Hz, and silently truncating fractional parts
        // would mask user mistakes (e.g. forgetting a trailing 'e6').
        double intpart{};
        if (std::modf(value, &intpart) != 0.0) {
            return std::nullopt;
        }

        return static_cast<std::uint64_t>(value);
    }

    ParseResult parseCliApp(CLI::App& app, int argc, char* argv[]) {
        try {
            app.parse(argc, argv);
        } catch (const CLI::CallForHelp&) {
            std::cout << app.help();
            return ParseResult::Help;
        } catch (const CLI::ParseError& e) {
            std::cerr << "[ERROR] " << e.what() << std::endl;
            std::cerr << app.help();
            return ParseResult::Error;
        }
        return ParseResult::Ok;
    }

    ParseResult assignFrequencyHz(std::string_view text, std::uint64_t& out) {
        const auto parsed{parseFrequencyHz(text)};
        if (parsed == std::nullopt) {
            std::cerr << "[ERROR] Invalid --freq: '" << text << "'" << std::endl;
            return ParseResult::Error;
        }
        out = parsed.value();
        return ParseResult::Ok;
    }
}  // namespace rpitx::cli
