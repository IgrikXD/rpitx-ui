/**
 * @file rds_pulse.h
 * @brief Pre-computed RDS biphase RRC pulse used by the RDS modulator.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 25.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstddef>
#include <span>

/**
 * @brief Length of the RDS biphase pulse, in 228 kHz samples.
 *
 * The pulse spans three RDS bit periods (192 samples per bit at 228 kHz),
 * which is the support of the RRC-shaped Manchester biphase symbol used by
 * the EN 50067 RDS standard. Successive pulses are added together with a
 * one-bit stride; the three-bit support gives the inter-symbol overlap
 * characteristic of the root-raised-cosine prototype.
 */
inline constexpr std::size_t RDS_PULSE_SAMPLES{576};

/**
 * @brief Number of 228 kHz samples per RDS bit (228000 / 1187.5 = 192).
 */
inline constexpr std::size_t RDS_SAMPLES_PER_BIT{192};

/**
 * @brief Read-only view of the RDS biphase pulse.
 *
 * The data is generated offline by Christophe Jacquet's generate_waveforms.py
 * (using Pydemod) and is part of the EN 50067 reference implementation; it
 * is convolved-once-and-stored to avoid running the RRC prototype filter
 * at run time on the Pi.
 *
 * @return Span over the 576-sample pulse.
 */
[[nodiscard]] std::span<const float> rdsPulse();
