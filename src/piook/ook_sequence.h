/**
 * @file ook_sequence.h
 * @brief Keying sequence of an on-off keying (OOK) transmission and the notations a message writes it in.
 *
 * On-off keying switches the carrier on and off; what the pulses and the gaps between them mean is defined
 * by the protocol of the receiver (pulse width, pulse distance, Manchester or another code), not by the
 * keying. A message therefore states the pulses and gaps themselves, in one of two notations:
 *   - symbols: a string of 1 and 0, the carrier on or off for one symbol time each (the unit a protocol
 *     counts its pulses and gaps in);
 *   - timings: the durations of the pulses and gaps in microseconds, as receivers and signal analysers
 *     list them.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace piook {
    /**
     * @brief Notation of a message.
     */
    enum class MessageType : uint8_t {
        Symbols,  ///< A string of 1 and 0: the carrier on or off for one symbol time each.
        Timings,  ///< Durations of the pulses and gaps in microseconds.
    };

    /**
     * @brief Interval of a keying sequence: the carrier on (a pulse) or off (a gap) for a duration.
     */
    struct KeyingInterval {
        bool carrierOn;
        double duration;  ///< Microseconds.
    };

    /**
     * @brief Keying sequence of a message of 1 and 0; white space between the symbols is ignored.
     *
     * @param message    Message of the Symbols type.
     * @param symbolTime Duration of a symbol in microseconds.
     * @return Sequence with every run of equal symbols as one interval, or std::nullopt if message has
     *         another character.
     */
    [[nodiscard]] std::optional<std::vector<KeyingInterval>> parseSymbols(std::string_view message, double symbolTime);

    /**
     * @brief Keying sequence of a message of durations in microseconds, separated by white space or commas.
     *
     * A negative duration is a gap and a positive one a pulse, as remote control code files list them; in a
     * message without a negative duration pulses and gaps take turns, starting with a pulse, as signal
     * analysers list them.
     *
     * @param message Message of the Timings type.
     * @return Sequence with one interval per duration, or std::nullopt if message has anything but finite
     *         numbers and their separators.
     */
    [[nodiscard]] std::optional<std::vector<KeyingInterval>> parseTimings(std::string_view message);
}  // namespace piook
