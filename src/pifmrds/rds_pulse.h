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
 * @brief Read-only view of the RDS biphase data-shaping pulse.
 *
 * Numerical realisation of the EN 50067 Annex A.1 pulse shape (Manchester
 * biphase symbol convolved with the standard's RRC data prototype). Pre-
 * computed and stored as a constant so the RRC filter does not have to run
 * at the 228 kHz MPX rate on the Pi. The sample values originate from
 * Christophe Jacquet's generate_waveforms.py (GPL-3.0); in-tree code has no
 * runtime or build dependency on that script.
 *
 * @return Span over the 576-sample pulse.
 */
[[nodiscard]] std::span<const float> rdsPulse();

/**
 * @brief Worst-case peak amplitude of the differentially-encoded RDS biphase
 *        baseband (overlap-add of three consecutive pulses, before subcarrier
 *        mixing).
 *
 * The RDS pulse spans three RDS bit periods and successive pulses are stamped
 * with a one-bit stride, so any output sample is the signed sum of three
 * pulse-tail contributions weighted by the differentially-encoded bits in
 * {-1, +1}. The worst case is reached when all three contributions add
 * constructively, i.e. sum of |pulse[k + n * RDS_SAMPLES_PER_BIT]| for
 * n in {0, 1, 2}, maximised over k in [0, RDS_SAMPLES_PER_BIT). This is a
 * tight upper bound: an adversarial bit stream can hit it exactly.
 *
 * Mixing onto the 57 kHz subcarrier multiplies by {0, +1, 0, -1} and so does
 * not change the peak. Dividing the modulator output by this constant
 * therefore normalises it to [-1, +1], which lets callers scale by an
 * absolute deviation budget (e.g. RDS_GAIN * peakDeviation in Hz) and get a
 * literal peak contribution rather than an empirical PiFmRds-tuned level.
 *
 * Computed once on first call via a function-local static; subsequent calls
 * return the cached value with no re-computation.
 *
 * @return Worst-case peak amplitude of the RDS biphase baseband before
 *         57 kHz subcarrier mixing.
 */
[[nodiscard]] float rdsBiphasePulseOverlapPeak();
