/**
 * @file ook_sequence.cpp
 * @brief Keying sequence parser implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "ook_sequence.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace piook {
    namespace {
        /**
         * @brief Whether ch is white space: a space, a tab or a line break, as in a sequence pasted from a file.
         */
        [[nodiscard]] constexpr bool isWhiteSpace(char ch) noexcept {
            return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
        }

        /**
         * @brief Whether ch separates two durations of a Timings message.
         */
        [[nodiscard]] constexpr bool isSeparator(char ch) noexcept {
            return isWhiteSpace(ch) || ch == ',';
        }
    }  // namespace

    std::optional<std::vector<KeyingInterval>> parseSymbols(std::string_view message, double symbolTime) {
        std::vector<KeyingInterval> sequence;
        for (const char symbol: message) {
            if (isWhiteSpace(symbol)) {
                continue;
            }
            if (symbol != '0' && symbol != '1') {
                return std::nullopt;
            }
            const bool carrierOn{symbol == '1'};
            if (sequence.empty() || sequence.back().carrierOn != carrierOn) {
                sequence.push_back({carrierOn, 0.0});
            }
            sequence.back().duration += symbolTime;
        }
        return sequence;
    }

    std::optional<std::vector<KeyingInterval>> parseTimings(std::string_view message) {
        std::vector<double> durations;
        const char* const last{message.data() + message.size()};
        for (const char* first{message.data()}; first != last;) {
            if (isSeparator(*first)) {
                ++first;
                continue;
            }
            double duration{};
            const auto [end, error]{std::from_chars(first, last, duration)};
            // A number ends at a separator or at the end of the message.
            if (error != std::errc{} || std::isfinite(duration) == false ||
                (end != last && isSeparator(*end) == false)) {
                return std::nullopt;
            }
            durations.push_back(duration);
            first = end;
        }

        const bool signedDurations{std::ranges::any_of(durations, [](double duration) { return duration < 0.0; })};
        std::vector<KeyingInterval> sequence;
        sequence.reserve(durations.size());
        for (std::size_t index{0}; index < durations.size(); ++index) {
            const bool carrierOn{signedDurations ? durations[index] > 0.0 : index % 2 == 0};
            sequence.push_back({carrierOn, std::abs(durations[index])});
        }
        return sequence;
    }
}  // namespace piook
