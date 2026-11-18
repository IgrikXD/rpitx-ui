/**
 * @file opera_encoder.cpp
 * @brief Opera transmission encoder implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 18.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "opera_encoder.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace piopera {
    namespace {
        /**
         * @brief Number of positions of a callsign in the code: two of the prefix, the digit and three of the suffix.
         */
        constexpr std::size_t CALLSIGN_POSITIONS{6};

        /**
         * @brief Position of the digit of a callsign; a prefix of one character stands right before it.
         */
        constexpr std::size_t DIGIT_POSITION{2};

        /**
         * @brief Largest number of letters of the suffix of a callsign.
         */
        constexpr std::size_t MAX_SUFFIX_LETTERS{CALLSIGN_POSITIONS - DIGIT_POSITION - 1};

        /**
         * @brief Number of bits of a packed callsign.
         */
        constexpr int CALLSIGN_BITS{28};

        /**
         * @brief Number of bits of the check value of a packed callsign.
         */
        constexpr int CHECK_VALUE_BITS{16};

        /**
         * @brief Number of check bits over a packed callsign and its check value.
         */
        constexpr int CHECK_BITS{3};

        /**
         * @brief Number of bits of a frame: 4 zero bits, the packed callsign, its check value and the check bits.
         */
        constexpr int FRAME_BITS{51};

        /**
         * @brief Generator polynomial of the CRC-16 of the check values, x^16 + x^15 + x^2 + 1, least
         *        significant bit first.
         */
        constexpr uint16_t CRC_POLYNOMIAL{0xA001};

        /**
         * @brief Sequence a frame is scrambled with, its first bit in bit 50.
         */
        constexpr uint64_t SCRAMBLING_SEQUENCE{0b111'0000'1010'1011'1111'0011'0110'1000'0000'1100'1000'1010'1011};

        /**
         * @brief Number of frame bits coded into one codeword of the Walsh-Hadamard code.
         */
        constexpr int DATA_BITS{3};

        /**
         * @brief Number of bits of a codeword of the Walsh-Hadamard code.
         */
        constexpr unsigned CODEWORD_BITS{7};

        /**
         * @brief Number of codewords of a transmission.
         */
        constexpr int CODEWORDS{FRAME_BITS / DATA_BITS};

        static_assert(1 + 2 * CODEWORDS * CODEWORD_BITS == SYMBOL_COUNT,
                      "a transmission must be a carrier-on symbol and a Manchester pair per codeword bit");

        /**
         * @brief Code of a character in the positions of a callsign: space 0, A - Z 1 - 26, 0 - 9 27 - 36.
         */
        [[nodiscard]] constexpr uint32_t characterCode(char ch) noexcept {
            if (ch >= 'A' && ch <= 'Z') {
                return static_cast<uint32_t>(ch - 'A') + 1;
            }
            if (ch >= '0' && ch <= '9') {
                return static_cast<uint32_t>(ch - '0') + 27;
            }
            return 0;
        }

        /**
         * @brief Place a callsign in the positions of the code, letters in upper case.
         *
         * The digit goes to DIGIT_POSITION, the prefix right before it and the suffix behind it;
         * the positions left over hold spaces.
         *
         * @return Positions, or std::nullopt if callsign is not one or two letters or digits, a digit
         *         and one to three letters.
         */
        [[nodiscard]] std::optional<std::array<char, CALLSIGN_POSITIONS>> callsignPositions(
            std::string_view callsign) noexcept {
            const auto isDigit{[](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; }};
            const auto isLetter{[](char ch) { return std::isalpha(static_cast<unsigned char>(ch)) != 0; }};
            const auto isLetterOrDigit{[](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0; }};

            // The digit is the third character behind a prefix of two, the second one otherwise.
            const std::size_t prefixLength{callsign.size() > DIGIT_POSITION && isDigit(callsign[DIGIT_POSITION]) ? 2U
                                                                                                                 : 1U};
            if (callsign.size() <= prefixLength || isDigit(callsign[prefixLength]) == false) {
                return std::nullopt;
            }
            const auto suffix{callsign.substr(prefixLength + 1)};
            if (std::ranges::all_of(callsign.substr(0, prefixLength), isLetterOrDigit) == false || suffix.empty() ||
                suffix.size() > MAX_SUFFIX_LETTERS || std::ranges::all_of(suffix, isLetter) == false) {
                return std::nullopt;
            }

            std::array<char, CALLSIGN_POSITIONS> positions{};
            positions.fill(' ');
            std::ranges::transform(callsign, positions.begin() + (DIGIT_POSITION - prefixLength), [](char ch) {
                return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            });
            return positions;
        }

        /**
         * @brief Pack the positions of a callsign into 28 bits.
         *
         * The first position takes 37 values (a space, a letter or a digit), the second one 36 (a
         * letter or a digit), the digit 10 and every position of the suffix 27 (a space or a letter).
         */
        [[nodiscard]] constexpr uint32_t packCallsign(const std::array<char, CALLSIGN_POSITIONS>& positions) noexcept {
            uint32_t packed{characterCode(positions[0])};
            packed = packed * 36 + characterCode(positions[1]) - characterCode('A');
            packed = packed * 10 + characterCode(positions[2]) - characterCode('0');
            for (std::size_t position{DIGIT_POSITION + 1}; position < CALLSIGN_POSITIONS; ++position) {
                packed = packed * 27 + characterCode(positions[position]);
            }
            return packed;
        }

        static_assert(37ULL * 36 * 10 * 27 * 27 * 27 <= (1ULL << CALLSIGN_BITS),
                      "every callsign must pack into CALLSIGN_BITS bits");

        /**
         * @brief Check value of the lowest count bits of bits, sent from the highest one on.
         *
         * The CRC-16 runs over the bits written out as the ASCII characters '0' and '1'. Its low
         * byte, 27 when it is zero, is followed by its high byte, 43 when it is zero.
         */
        [[nodiscard]] constexpr uint16_t checkValue(uint64_t bits, int count) noexcept {
            uint16_t crc{0};
            for (int position{count - 1}; position >= 0; --position) {
                const unsigned character{'0' + static_cast<unsigned>((bits >> position) & 1U)};
                for (int bit{0}; bit < 8; ++bit) {
                    const bool feedback{(((character >> bit) ^ crc) & 1U) != 0};
                    crc = static_cast<uint16_t>(crc >> 1);
                    if (feedback) {
                        crc ^= CRC_POLYNOMIAL;
                    }
                }
            }
            const unsigned low{(crc & 0xFFU) != 0 ? crc & 0xFFU : 27U};
            const unsigned high{(crc >> 8) != 0 ? crc >> 8U : 43U};
            return static_cast<uint16_t>((low << 8) | high);
        }
    }  // namespace

    bool isEncodable(std::string_view callsign) noexcept {
        return callsignPositions(callsign) != std::nullopt;
    }

    std::array<bool, SYMBOL_COUNT> encodeTransmission(std::string_view callsign) {
        const auto positions{callsignPositions(callsign)};
        if (positions == std::nullopt) {
            throw std::invalid_argument{"the callsign cannot be encoded"};
        }

        // Frame: 4 zero bits, the packed callsign, its check value and the check bits over both.
        const uint64_t packed{packCallsign(positions.value())};
        const uint64_t checked{(packed << CHECK_VALUE_BITS) | checkValue(packed, CALLSIGN_BITS)};
        const uint64_t checkBits{checkValue(checked, CALLSIGN_BITS + CHECK_VALUE_BITS) & ((1U << CHECK_BITS) - 1U)};
        const uint64_t scrambled{((checked << CHECK_BITS) | checkBits) ^ SCRAMBLING_SEQUENCE};

        // Codeword bits column by column: bit n of a codeword is the parity of the 3 frame bits that n + 1 selects.
        std::array<bool, SYMBOL_COUNT> symbols{};
        symbols.front() = true;
        auto symbol{symbols.begin() + 1};
        for (unsigned column{1}; column <= CODEWORD_BITS; ++column) {
            for (int codeword{0}; codeword < CODEWORDS; ++codeword) {
                const auto data{static_cast<unsigned>(scrambled >> (FRAME_BITS - DATA_BITS * (codeword + 1))) & 0b111U};
                const bool bit{(std::popcount(data & column) & 1) != 0};
                *symbol++ = bit == false;
                *symbol++ = bit;
            }
        }
        return symbols;
    }
}  // namespace piopera
