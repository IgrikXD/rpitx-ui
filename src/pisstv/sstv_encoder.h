/**
 * @file sstv_encoder.h
 * @brief Encoding of an SSTV transmission into tones: the calibration header and the scan lines of a picture.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 13.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "sstv_mode.h"

namespace pisstv {
    /**
     * @brief Append the calibration header of mode to tones.
     *
     * The header is the leader, break and leader tones followed by the VIS code
     * of the mode between a start and a stop bit: seven bits, least significant
     * first, and an even parity bit. The sync pulse a mode sends in front of its
     * first line closes the header.
     *
     * @param mode  SSTV mode the header announces.
     * @param tones Destination.
     */
    void appendCalibrationHeader(const SstvMode& mode, std::vector<Tone>& tones);

    /**
     * @brief Append one pass of the timing sequence of mode to tones.
     *
     * A scan sends one tone per pixel between BLACK_HZ and WHITE_HZ. Y, R-Y and
     * B-Y are scaled as in ITU-R BT.601: Y within 16..235, R-Y and B-Y within
     * 16..240 of the 0..255 range.
     *
     * @param mode  SSTV mode of the picture.
     * @param rows  The rowsPerSequence(mode) picture rows the pass carries, one after another,
     *              three bytes per pixel: red, green, blue.
     * @param tones Destination.
     *
     * @throws std::invalid_argument when rows does not hold rowsPerSequence(mode) rows of mode.width pixels.
     */
    void appendSequence(const SstvMode& mode, std::span<const std::uint8_t> rows, std::vector<Tone>& tones);
}  // namespace pisstv
