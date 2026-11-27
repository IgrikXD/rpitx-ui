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
         * @brief White space of a message: a space, a tab or a line break, as in a sequence pasted from a file.
         */
        constexpr std::string_view WHITE_SPACE{" \t\n\r"};

        /**
         * @brief Characters between two durations of a Timings message: white space or a comma.
         */
        constexpr std::string_view SEPARATORS{" \t\n\r,"};
    }  // namespace

    std::optional<double> parseDuration(std::string_view text) {
        double duration{};
        const char* const last{text.data() + text.size()};
        if (const auto [end, error]{std::from_chars(text.data(), last, duration)};
            error != std::errc{} || end != last || std::isfinite(duration) == false) {
            return std::nullopt;
        }
        return duration;
    }

    std::optional<std::vector<KeyingInterval>> parseSymbols(std::string_view message, double symbolTime) {
        std::vector<KeyingInterval> sequence;
        for (const char symbol: message) {
            if (WHITE_SPACE.find(symbol) != std::string_view::npos) {
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
        for (auto first{message.find_first_not_of(SEPARATORS)}; first != std::string_view::npos;) {
            // A duration runs up to the next separator or to the end of the message.
            const auto last{std::min(message.find_first_of(SEPARATORS, first), message.size())};
            const auto duration{parseDuration(message.substr(first, last - first))};
            if (duration == std::nullopt) {
                return std::nullopt;
            }
            durations.push_back(duration.value());
            first = message.find_first_not_of(SEPARATORS, last);
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
