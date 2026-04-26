/**
 * @file byte_utils.h
 * @brief Byte packing helpers shared by protocol encoders.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 26.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>

/**
 * @brief Pack two bytes into a 16-bit value, big-endian (MSB first).
 */
[[nodiscard]] constexpr uint16_t packUint16BigEndian(uint8_t high, uint8_t low) {
    return static_cast<uint16_t>((static_cast<uint16_t>(high) << 8U) | static_cast<uint16_t>(low));
}
