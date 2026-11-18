/**
 * @file opera_encoder.h
 * @brief Encoding of a callsign into the symbols of an Opera beacon transmission.
 *
 * Follows the coding of the Opera program by EA5HVK: the callsign packed into 28
 * bits, two CRC check values, scrambling, a Walsh-Hadamard block code,
 * interleaving and Manchester coding, sent as on-off keying of the carrier.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 18.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace piopera {
    /**
     * @brief Number of symbols of an Opera transmission: a carrier-on symbol and two per coded bit.
     */
    inline constexpr std::size_t SYMBOL_COUNT{239};

    /**
     * @brief Whether the Opera code can carry callsign: one or two letters or digits, a digit and one
     *        to three letters, in upper or lower case.
     */
    [[nodiscard]] bool isEncodable(std::string_view callsign) noexcept;

    /**
     * @brief Encode a callsign as the symbols of one Opera transmission.
     *
     * The 28-bit packed callsign, its 16-bit check value and 3 check bits over both
     * follow 4 zero bits. The 51 bits are scrambled, coded 3 at a time into the 7-bit
     * codewords of a Walsh-Hadamard code, sent codeword bit by codeword bit (column by
     * column) and Manchester coded behind a carrier-on symbol: a 0 as carrier on, off
     * and a 1 as carrier off, on.
     *
     * @param callsign Callsign isEncodable() accepts; letters are encoded in upper case.
     * @return Symbols in the order of transmission, true for carrier on.
     *
     * @throws std::invalid_argument when callsign cannot be encoded.
     */
    [[nodiscard]] std::array<bool, SYMBOL_COUNT> encodeTransmission(std::string_view callsign);
}  // namespace piopera
