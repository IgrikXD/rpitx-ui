/**
 * @file spectrum_painter.h
 * @brief Pixel-row to frequency / amplitude sample mapping for spectrum painting.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 09.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace pispectrumpaint {
    /**
     * @brief Number of carrier amplitude steps above "off".
     *
     * The GPIO pad has eight drive strengths; the two weakest ones proved unusable
     * for the carrier (one emits nothing, the other more than the next one), so
     * only strengths 2..7 are used.
     */
    inline constexpr int AMPLITUDE_LEVELS{6};

    /**
     * @brief Pad drive strength of the weakest carrier-on level.
     */
    inline constexpr int WEAKEST_DRIVE_STRENGTH{2};

    /**
     * @brief Largest 8-bit gray value (white).
     */
    inline constexpr int GRAY_MAX{255};

    /**
     * @brief Sample that switches the carrier off at the centre frequency.
     */
    inline constexpr std::complex<float> CARRIER_OFF_SAMPLE{0.0F, 0.0F};

    /**
     * @brief Map an 8-bit gray value to the amplitude value of an iqdmasync MODE_FREQ_A sample.
     *
     * Rounds gray / 255 * AMPLITUDE_LEVELS to the nearest step: black (level 0)
     * switches the carrier off, white selects the strongest pad drive. iqdmasync
     * turns an amplitude value of 0 into "pin off" and a value of n + 1 into pad
     * drive strength n, so level 1 becomes WEAKEST_DRIVE_STRENGTH and level
     * AMPLITUDE_LEVELS the strongest drive, 7.
     */
    [[nodiscard]] constexpr float amplitudeLevel(std::uint8_t gray) noexcept {
        const int level{(gray * AMPLITUDE_LEVELS + GRAY_MAX / 2) / GRAY_MAX};
        return level == 0 ? 0.0F : static_cast<float>(level + WEAKEST_DRIVE_STRENGTH);
    }

    /**
     * @brief Frequency offset of a pixel column relative to the carrier, in Hz.
     *
     * Pixel centres are spread symmetrically over [-bandwidth / 2, +bandwidth / 2]:
     * column 0 lies half a step above the lower edge and the last column half a
     * step below the upper edge.
     */
    [[nodiscard]] constexpr float frequencyOffset(std::size_t column, std::size_t width, float bandwidth) noexcept {
        return (static_cast<float>(column) + 0.5F) * (bandwidth / static_cast<float>(width)) - bandwidth * 0.5F;
    }

    /**
     * @brief Convert one picture row into the frequency / amplitude samples of iqdmasync MODE_FREQ_A.
     * @param row       Gray values of the row, one per pixel column.
     * @param bandwidth Painted RF bandwidth in Hz.
     * @param samples   Destination: real part = frequency offset in Hz, imaginary part = amplitude level.
     *
     * @throws std::invalid_argument when row is empty or samples.size() differs from row.size().
     */
    inline void paintRow(std::span<const std::uint8_t> row, float bandwidth, std::span<std::complex<float>> samples) {
        if (row.empty() || samples.size() != row.size()) {
            throw std::invalid_argument{"paintRow: samples size " + std::to_string(samples.size()) +
                                        " must match a non-empty row of " + std::to_string(row.size()) + " pixels"};
        }
        for (std::size_t column{0}; column < row.size(); ++column) {
            samples[column] = {frequencyOffset(column, row.size(), bandwidth), amplitudeLevel(row[column])};
        }
    }
}  // namespace pispectrumpaint
