/**
 * @file pocsag_encoder.cpp
 * @brief POCSAG transmission encoder implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 16.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pocsag_encoder.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>

namespace pipocsag {
    namespace {
        /**
         * @brief Synchronization codeword that opens every batch.
         */
        constexpr std::uint32_t SYNC_CODEWORD{0x7CD215D8};

        /**
         * @brief Idle codeword, sent wherever a batch has no address or message codeword to carry.
         *
         * Table 2 of the Recommendation prints its bit 10 as a 1 (0x7AC9C197), which is not a
         * codeword of the code; this is the value receivers use.
         */
        constexpr std::uint32_t IDLE_CODEWORD{0x7A89C197};

        /**
         * @brief Word of the preamble: reversals that start with a 1.
         */
        constexpr std::uint32_t PREAMBLE_WORD{0xAAAAAAAA};

        /**
         * @brief Number of words of the preamble: 576 bits, the duration of a batch plus a codeword.
         */
        constexpr std::size_t PREAMBLE_WORDS{18};

        /**
         * @brief Number of codewords of a batch behind its synchronization codeword: 8 frames of 2 codewords.
         */
        constexpr std::size_t BATCH_CODEWORDS{16};

        /**
         * @brief Number of codewords of a frame.
         */
        constexpr std::size_t FRAME_CODEWORDS{2};

        /**
         * @brief Number of check bits of a codeword.
         */
        constexpr int CHECK_BITS{10};

        /**
         * @brief Generator polynomial of the check bits: x^10 + x^9 + x^8 + x^6 + x^5 + x^3 + 1.
         */
        constexpr std::uint32_t GENERATOR_POLYNOMIAL{0b111'0110'1001};

        /**
         * @brief Number of message bits of a message codeword.
         */
        constexpr int MESSAGE_BITS{20};

        /**
         * @brief Flag bit of a codeword: set in a message codeword, clear in an address codeword.
         */
        constexpr std::uint32_t MESSAGE_FLAG{1U << MESSAGE_BITS};

        /**
         * @brief Number of function bits of an address codeword, behind its address bits.
         */
        constexpr int FUNCTION_BITS{2};

        /**
         * @brief Number of bits of a character of the numeric-only format.
         */
        constexpr int NUMERIC_CHARACTER_BITS{4};

        /**
         * @brief Number of bits of a character of the alphanumeric format.
         */
        constexpr int ALPHANUMERIC_CHARACTER_BITS{7};

        /**
         * @brief Code of the space of the numeric-only format, which also fills its last codeword.
         */
        constexpr std::uint8_t NUMERIC_SPACE{0xC};

        /**
         * @brief Build a codeword from its 21 information bits.
         *
         * The check bits are the remainder of the information bits, taken as the coefficients of
         * x^30 .. x^10, divided modulo 2 by the generator polynomial; an even parity bit follows them.
         *
         * @param information Flag bit and the 20 bits behind it, the flag in bit 20.
         * @return Codeword, the first bit on the air in bit 31.
         */
        [[nodiscard]] constexpr std::uint32_t makeCodeword(std::uint32_t information) noexcept {
            std::uint32_t remainder{information << CHECK_BITS};
            for (int bit{MESSAGE_BITS + CHECK_BITS}; bit >= CHECK_BITS; --bit) {
                if (((remainder >> bit) & 1U) != 0) {
                    remainder ^= GENERATOR_POLYNOMIAL << (bit - CHECK_BITS);
                }
            }
            const std::uint32_t block{(information << CHECK_BITS) | remainder};
            return (block << 1) | (static_cast<std::uint32_t>(std::popcount(block)) & 1U);
        }

        static_assert(makeCodeword(SYNC_CODEWORD >> (CHECK_BITS + 1)) == SYNC_CODEWORD &&
                          makeCodeword(IDLE_CODEWORD >> (CHECK_BITS + 1)) == IDLE_CODEWORD,
                      "makeCodeword must reproduce the synchronization and the idle codeword");

        /**
         * @brief Build the address codeword of a pager: its address without the frame bits and the function bits.
         */
        [[nodiscard]] constexpr std::uint32_t makeAddressCodeword(std::uint32_t address, Function function) noexcept {
            return makeCodeword(((address >> FRAME_BITS) << FUNCTION_BITS) | static_cast<std::uint32_t>(function));
        }

        static_assert(
            makeAddressCodeword(IDLE_ADDRESS, Function::A) == IDLE_CODEWORD &&
                makeAddressCodeword(SYNC_ADDRESS, Function::C) == SYNC_CODEWORD,
            "IDLE_ADDRESS and SYNC_ADDRESS must be addresses of the idle and of the synchronization codeword");

        /**
         * @brief Look up the 4-bit code of a character of the numeric-only format.
         *
         * The code 0xA is spare. Receivers print the brackets in either shape, so both are accepted.
         *
         * @return Code of the character, or std::nullopt if the format does not have it.
         */
        [[nodiscard]] constexpr std::optional<std::uint8_t> numericCode(char ch) noexcept {
            switch (ch) {
                case 'U':
                    return 0xB;
                case ' ':
                    return NUMERIC_SPACE;
                case '-':
                    return 0xD;
                case ']':
                case ')':
                    return 0xE;
                case '[':
                case '(':
                    return 0xF;
                default:
                    if (ch >= '0' && ch <= '9') {
                        return static_cast<std::uint8_t>(ch - '0');
                    }
                    return std::nullopt;
            }
        }

        /**
         * @brief Append the message codewords of a message to codewords.
         *
         * The bits of every character are sent from bit 0 on and fill the 20 message bits of one
         * codeword after another. The last codeword is completed with space characters in the
         * numeric-only format and with zero bits (Null) in the alphanumeric one.
         */
        void appendMessage(MessageType type, std::string_view message, std::vector<std::uint32_t>& codewords) {
            // Message bits of the codeword under construction, the first one on the air highest.
            std::uint32_t messageBits{0};
            int bitCount{0};
            const auto append{[&codewords, &messageBits, &bitCount](std::uint32_t character, int characterBits) {
                for (int bit{0}; bit < characterBits; ++bit) {
                    messageBits = (messageBits << 1) | ((character >> bit) & 1U);
                    if (++bitCount == MESSAGE_BITS) {
                        codewords.push_back(makeCodeword(MESSAGE_FLAG | messageBits));
                        messageBits = 0;
                        bitCount    = 0;
                    }
                }
            }};

            if (type == MessageType::Numeric) {
                for (const char ch: message) {
                    append(numericCode(ch).value(), NUMERIC_CHARACTER_BITS);
                }
                while (bitCount != 0) {
                    append(NUMERIC_SPACE, NUMERIC_CHARACTER_BITS);
                }
            } else {
                for (const char ch: message) {
                    append(static_cast<unsigned char>(ch), ALPHANUMERIC_CHARACTER_BITS);
                }
                if (bitCount != 0) {
                    append(0, MESSAGE_BITS - bitCount);
                }
            }
        }
    }  // namespace

    bool isEncodable(MessageType type, std::string_view message) noexcept {
        // No default branch: MessageType is closed-set, so -Wswitch flags any future
        // enumerator that forgets to update this dispatch.
        switch (type) {
            case MessageType::Alphanumeric:
                return std::ranges::all_of(message, [](char ch) {
                    return (static_cast<unsigned char>(ch) >> ALPHANUMERIC_CHARACTER_BITS) == 0;
                });
            case MessageType::Numeric:
                return std::ranges::all_of(message, [](char ch) { return numericCode(ch) != std::nullopt; });
            case MessageType::ToneOnly:
                return message.empty();
        }
        return false;
    }

    std::vector<std::uint32_t> encodeTransmission(std::uint32_t address, Function function, MessageType type,
                                                  std::string_view message) {
        if (isPagerAddress(address) == false || isEncodable(type, message) == false) {
            throw std::invalid_argument{"the pager address or the message cannot be encoded"};
        }

        // Codewords of the batches behind their synchronization codewords: idle up to the frame of
        // the pager, the address, the message, the idle codeword that ends it, idle to the end of the batch.
        const std::size_t frame{address & ((1U << FRAME_BITS) - 1U)};
        std::vector<std::uint32_t> codewords(FRAME_CODEWORDS * frame, IDLE_CODEWORD);
        codewords.push_back(makeAddressCodeword(address, function));
        appendMessage(type, message, codewords);
        codewords.push_back(IDLE_CODEWORD);
        codewords.resize((codewords.size() + BATCH_CODEWORDS - 1) / BATCH_CODEWORDS * BATCH_CODEWORDS, IDLE_CODEWORD);

        std::vector<std::uint32_t> words(PREAMBLE_WORDS, PREAMBLE_WORD);
        for (std::size_t offset{0}; offset < codewords.size(); offset += BATCH_CODEWORDS) {
            const auto batch{std::span{codewords}.subspan(offset, BATCH_CODEWORDS)};
            words.push_back(SYNC_CODEWORD);
            words.insert(words.end(), batch.begin(), batch.end());
        }
        return words;
    }
}  // namespace pipocsag
