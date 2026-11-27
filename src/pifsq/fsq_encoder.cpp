/**
 * @file fsq_encoder.cpp
 * @brief FSQ transmission encoder implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "fsq_encoder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>

namespace pifsq {
    namespace {
        /**
         * @brief Second symbol of a character that takes a single symbol.
         */
        constexpr uint8_t NO_SECOND_SYMBOL{0};

        /**
         * @brief Symbols of a character of the FSQ varicode.
         */
        struct Varicode {
            uint8_t first;   ///< First symbol, 0 - 28.
            uint8_t second;  ///< Second symbol, 29 - 31, or NO_SECOND_SYMBOL.
        };

        /**
         * @brief Characters of the FSQ varicode that share a second symbol.
         */
        struct VaricodeRow {
            uint8_t second;               ///< Second symbol of the characters, or NO_SECOND_SYMBOL.
            std::string_view characters;  ///< Characters in the order of their first symbol, from 0 on.
        };

        /**
         * @brief Printable characters of the FSQ varicode: the lower-case letters, the space and the full stop
         *        take one symbol, every other character two.
         */
        // clang-format off
        constexpr std::array<VaricodeRow, 4> VARICODE_ROWS{{
            {NO_SECOND_SYMBOL, " abcdefghijklmnopqrstuvwxyz."},
            {29,               "@ABCDEFGHIJKLMNOPQRSTUVWXYZ,?"},
            {30,               "~1234567890!\"#$%&'()*+-/:;<>"},
            {31,               "=[\\]^_{|}`"},
        }};
        // clang-format on

        /**
         * @brief Symbol of the space.
         */
        constexpr uint8_t SPACE{0};

        /**
         * @brief Symbol of the line feed, the marker that opens a sentence.
         */
        constexpr uint8_t LINE_FEED{28};

        /**
         * @brief Symbols of the backspace, the marker that ends a directed sentence.
         */
        constexpr Varicode BACKSPACE{27, 31};

        /**
         * @brief Generator polynomial of the checksum of a callsign, x^8 + x^2 + x + 1, without its highest term.
         */
        constexpr uint8_t CHECKSUM_POLYNOMIAL{0x07};

        /**
         * @brief Digits the checksum of a callsign is written in.
         */
        constexpr std::string_view HEXADECIMAL_DIGITS{"0123456789abcdef"};

        /**
         * @brief Look up the symbols of a character.
         * @return Symbols, or std::nullopt if ch is not a printable ASCII character.
         */
        [[nodiscard]] constexpr std::optional<Varicode> varicode(char ch) noexcept {
            for (const VaricodeRow& row: VARICODE_ROWS) {
                if (const auto first{row.characters.find(ch)}; first != std::string_view::npos) {
                    return Varicode{static_cast<uint8_t>(first), row.second};
                }
            }
            return std::nullopt;
        }

        static_assert(std::ranges::all_of(std::views::iota(' ', '\x7F'),
                                          [](char ch) { return varicode(ch) != std::nullopt; }),
                      "VARICODE_ROWS must hold every printable ASCII character");

        /**
         * @brief Checksum of a callsign: its CRC-8 from the initial value 0, the highest bit of every character first.
         */
        [[nodiscard]] constexpr uint8_t checksum(std::string_view callsign) noexcept {
            uint8_t crc{0};
            for (const char ch: callsign) {
                crc ^= static_cast<uint8_t>(ch);
                for (int bit{0}; bit < 8; ++bit) {
                    crc = static_cast<uint8_t>((crc << 1) ^ ((crc & 0x80) != 0 ? CHECKSUM_POLYNOMIAL : 0));
                }
            }
            return crc;
        }

        static_assert(checksum("123456789") == 0xF4, "checksum must be the CRC-8 of CHECKSUM_POLYNOMIAL");
    }  // namespace

    bool isCallsign(std::string_view callsign) noexcept {
        return callsign.empty() == false && callsign.size() <= MAX_CALLSIGN_LENGTH &&
               std::ranges::all_of(
                   callsign, [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '/'; });
    }

    bool isEncodable(std::string_view message) noexcept {
        return std::ranges::all_of(message, [](char ch) { return varicode(ch) != std::nullopt; });
    }

    std::vector<uint8_t> encodeTransmission(MessageType type, std::string_view callsign, std::string_view destination,
                                            std::string_view message) {
        const bool directed{type == MessageType::Directed};
        if (isCallsign(callsign) == false || (directed && isCallsign(destination) == false) ||
            isEncodable(message) == false) {
            throw std::invalid_argument{"a callsign or the message cannot be encoded"};
        }

        // Text of the sentence between its markers.
        std::string text{callsign};
        text += ':';
        if (directed) {
            const uint8_t check{checksum(callsign)};
            text += HEXADECIMAL_DIGITS[check >> 4];
            text += HEXADECIMAL_DIGITS[check & 0xF];
            text += destination;
            text += ' ';
        }
        text += message;

        std::vector<uint8_t> symbols{SPACE, SPACE, LINE_FEED};
        for (const char ch: text) {
            const Varicode code{varicode(ch).value()};
            symbols.push_back(code.first);
            if (code.second != NO_SECOND_SYMBOL) {
                symbols.push_back(code.second);
            }
        }
        if (directed) {
            symbols.insert(symbols.end(), {SPACE, SPACE, BACKSPACE.first, BACKSPACE.second, SPACE, SPACE});
        } else {
            symbols.insert(symbols.end(), {LINE_FEED, SPACE});
        }

        // IFK+: a symbol moves the tone up by one step more than its value, around the tones.
        uint8_t tone{0};
        for (uint8_t& symbol: symbols) {
            tone   = static_cast<uint8_t>((tone + symbol + 1) % TONE_COUNT);
            symbol = tone;
        }
        return symbols;
    }
}  // namespace pifsq
