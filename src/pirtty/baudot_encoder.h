/**
 * @file baudot_encoder.h
 * @brief Encoding of text into the five-bit Baudot code (ITA2) of radioteletype.
 *
 * The figures case follows the US teleprinters ("$" on D, BELL on S), the
 * variant amateur RTTY uses and fldigi and minimodem decode by default.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 14.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace pirtty {
    /**
     * @brief Number of data bits of a Baudot code.
     */
    inline constexpr int BAUDOT_CODE_BITS{5};

    /**
     * @brief Case a receiver prints a Baudot code in.
     */
    enum class BaudotCase : std::uint8_t {
        Letters,  ///< Printed in letters case (after LTRS).
        Figures,  ///< Printed in figures case (after FIGS).
        Both,     ///< Printed in either case (space, carriage return, line feed).
    };

    /**
     * @brief Baudot code of an ASCII character.
     */
    struct BaudotCharacter {
        char ch;                ///< ASCII character (upper case for letters).
        std::uint8_t code;      ///< Five-bit code, the first data bit on the air as bit 0.
        BaudotCase letterCase;  ///< Case the code prints the character in.
    };

    /**
     * @brief ITA2 alphabet with the figures case of the US teleprinters.
     *
     * Entries are sorted by ASCII code to allow binary-search lookup.
     */
    // clang-format off
    inline constexpr std::array<BaudotCharacter, 55> BAUDOT_TABLE{{
        {'\a', 0x05, BaudotCase::Figures},
        {'\n', 0x02, BaudotCase::Both},
        {'\r', 0x08, BaudotCase::Both},
        {' ',  0x04, BaudotCase::Both},
        {'!',  0x0D, BaudotCase::Figures},
        {'"',  0x11, BaudotCase::Figures},
        {'#',  0x14, BaudotCase::Figures},
        {'$',  0x09, BaudotCase::Figures},
        {'&',  0x1A, BaudotCase::Figures},
        {'\'', 0x0B, BaudotCase::Figures},
        {'(',  0x0F, BaudotCase::Figures},
        {')',  0x12, BaudotCase::Figures},
        {',',  0x0C, BaudotCase::Figures},
        {'-',  0x03, BaudotCase::Figures},
        {'.',  0x1C, BaudotCase::Figures},
        {'/',  0x1D, BaudotCase::Figures},
        {'0',  0x16, BaudotCase::Figures},
        {'1',  0x17, BaudotCase::Figures},
        {'2',  0x13, BaudotCase::Figures},
        {'3',  0x01, BaudotCase::Figures},
        {'4',  0x0A, BaudotCase::Figures},
        {'5',  0x10, BaudotCase::Figures},
        {'6',  0x15, BaudotCase::Figures},
        {'7',  0x07, BaudotCase::Figures},
        {'8',  0x06, BaudotCase::Figures},
        {'9',  0x18, BaudotCase::Figures},
        {':',  0x0E, BaudotCase::Figures},
        {';',  0x1E, BaudotCase::Figures},
        {'?',  0x19, BaudotCase::Figures},
        {'A',  0x03, BaudotCase::Letters},
        {'B',  0x19, BaudotCase::Letters},
        {'C',  0x0E, BaudotCase::Letters},
        {'D',  0x09, BaudotCase::Letters},
        {'E',  0x01, BaudotCase::Letters},
        {'F',  0x0D, BaudotCase::Letters},
        {'G',  0x1A, BaudotCase::Letters},
        {'H',  0x14, BaudotCase::Letters},
        {'I',  0x06, BaudotCase::Letters},
        {'J',  0x0B, BaudotCase::Letters},
        {'K',  0x0F, BaudotCase::Letters},
        {'L',  0x12, BaudotCase::Letters},
        {'M',  0x1C, BaudotCase::Letters},
        {'N',  0x0C, BaudotCase::Letters},
        {'O',  0x18, BaudotCase::Letters},
        {'P',  0x16, BaudotCase::Letters},
        {'Q',  0x17, BaudotCase::Letters},
        {'R',  0x0A, BaudotCase::Letters},
        {'S',  0x05, BaudotCase::Letters},
        {'T',  0x10, BaudotCase::Letters},
        {'U',  0x07, BaudotCase::Letters},
        {'V',  0x1E, BaudotCase::Letters},
        {'W',  0x13, BaudotCase::Letters},
        {'X',  0x1D, BaudotCase::Letters},
        {'Y',  0x15, BaudotCase::Letters},
        {'Z',  0x11, BaudotCase::Letters},
    }};
    // clang-format on

    static_assert(std::ranges::is_sorted(BAUDOT_TABLE, {}, &BaudotCharacter::ch),
                  "BAUDOT_TABLE must be sorted by 'ch' in ascending ASCII order for lower_bound lookup");

    /**
     * @brief Look up the Baudot code of an ASCII character.
     * @param ch Input character (case-insensitive for letters).
     * @return Baudot code and case, or std::nullopt if ITA2 has no such character.
     */
    [[nodiscard]] inline std::optional<BaudotCharacter> charToBaudot(char ch) {
        const auto upper{static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))};
        const auto it{std::ranges::lower_bound(BAUDOT_TABLE, upper, {}, &BaudotCharacter::ch)};
        if (it != BAUDOT_TABLE.end() && it->ch == upper) {
            return *it;
        }
        return std::nullopt;
    }

    /**
     * @brief Baudot codes of one RTTY transmission.
     */
    struct EncodedMessage {
        std::vector<std::uint8_t> codes;   ///< Codes in the order of transmission.
        std::size_t skippedCharacters{0};  ///< Characters of the message that ITA2 does not have.
    };

    /**
     * @brief Encode a message as the Baudot codes of one RTTY transmission.
     *
     * Two LTRS codes the receiver synchronises on and a carriage return and line
     * feed lead in, a carriage return and line feed end the message. A letter or
     * figure is preceded by LTRS or FIGS whenever the receiver may be in the other
     * case, also after a space in figures case: a receiver that unshifts on space
     * returns to letters there. Characters ITA2 does not have are skipped.
     *
     * @param message Text to encode; letters are sent in upper case.
     */
    [[nodiscard]] EncodedMessage encodeMessage(std::string_view message);
}  // namespace pirtty
