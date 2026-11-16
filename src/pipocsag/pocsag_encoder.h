/**
 * @file pocsag_encoder.h
 * @brief Encoding of a pager call into the words of a POCSAG transmission.
 *
 * Follows Recommendation ITU-R M.584-2, Annex I (Radio-paging code No. 1).
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 16.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace pipocsag {
    /**
     * @brief Number of bits of a word of the transmission: a codeword or a part of the preamble.
     */
    inline constexpr int WORD_BITS{32};

    /**
     * @brief Number of the lowest bits of a pager address that select its frame of the batch instead of being sent.
     */
    inline constexpr int FRAME_BITS{3};

    /**
     * @brief Highest pager address: the identity of a pager is 21 bits wide.
     */
    inline constexpr std::uint32_t MAX_ADDRESS{(1U << 21) - 1};

    /**
     * @brief First of the eight addresses whose address codeword of function A is the idle codeword.
     */
    inline constexpr std::uint32_t IDLE_ADDRESS{2'007'664};

    /**
     * @brief First of the eight addresses whose address codeword of function C is the synchronization codeword.
     */
    inline constexpr std::uint32_t SYNC_ADDRESS{2'045'056};

    /**
     * @brief Format of the message that follows the address of a pager.
     */
    enum class MessageType : std::uint8_t {
        Alphanumeric,  ///< Alphanumeric format, 7 bits per character.
        Numeric,       ///< Numeric-only format, 4 bits per character.
        ToneOnly,      ///< No message: the pager only alerts.
    };

    /**
     * @brief Function bits of an address codeword: one of the four addresses of a pager.
     */
    enum class Function : std::uint8_t {
        A,  ///< Function bits 00, the address of numeric-only messages.
        B,  ///< Function bits 01.
        C,  ///< Function bits 10.
        D,  ///< Function bits 11, the address of alphanumeric messages.
    };

    /**
     * @brief Whether a pager can have the address: a 21-bit identity other than the eight addresses
     *        of the idle codeword and the eight of the synchronization codeword.
     */
    [[nodiscard]] constexpr bool isPagerAddress(std::uint32_t address) noexcept {
        const std::uint32_t addressBits{address >> FRAME_BITS};
        return address <= MAX_ADDRESS && addressBits != (IDLE_ADDRESS >> FRAME_BITS) &&
               addressBits != (SYNC_ADDRESS >> FRAME_BITS);
    }

    /**
     * @brief Whether the format of type has every character of message.
     *
     * The numeric-only format has the digits, the space, "U" (urgency), "-" and the brackets
     * "[" "]", also accepted as "(" ")"; the alphanumeric format has the 7-bit characters of the
     * International Alphabet No. 5 (ASCII). A tone-only call has no message.
     */
    [[nodiscard]] bool isEncodable(MessageType type, std::string_view message) noexcept;

    /**
     * @brief Encode a pager call as the 32-bit words of one transmission.
     *
     * The preamble, 576 bits of reversals, is followed by whole batches: a synchronization
     * codeword and 16 codewords each. Idle codewords lead up to the frame of the pager, where
     * the address codeword is sent; the message codewords follow it, an idle codeword ends the
     * message and idle codewords complete the batch.
     *
     * @param address  Pager address, one isPagerAddress() accepts.
     * @param function Function bits of the address codeword.
     * @param type     Format of the message.
     * @param message  Message, one isEncodable() accepts for type.
     * @return Words in the order of transmission, each sent from bit 31 on.
     *
     * @throws std::invalid_argument when the address or the message cannot be encoded.
     */
    [[nodiscard]] std::vector<std::uint32_t> encodeTransmission(std::uint32_t address, Function function,
                                                                MessageType type, std::string_view message);
}  // namespace pipocsag
