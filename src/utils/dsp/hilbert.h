/**
 * @file hilbert.h
 * @brief Hilbert transform FIR filter for analytic signal generation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <vector>

#include "iq_sample.h"

/**
 * @brief Hilbert FIR transformer with Blackman window.
 *
 * Produces an analytic signal (I + jQ) from a real input.
 * The I channel is delayed to match the group delay of the
 * FIR Hilbert filter on the Q channel.
 *
 * The number of filter taps is specified at construction time,
 * allowing callers to trade precision/sidelobe suppression
 * against memory usage and group delay.
 *
 * @code
 * Hilbert hilbert{255};
 * auto iq{hilbert.process(sample)};
 * @endcode
 */
class Hilbert {
public:
    /**
     * @brief Default number of FIR filter taps.
     */
    static constexpr int DEFAULT_TAPS{255};

    /**
     * @brief Construct the Hilbert transformer and compute FIR coefficients.
     *
     * Coefficients use a Blackman window for sidelobe suppression.
     * Only odd-indexed taps (relative to center) are non-zero.
     *
     * @param taps Number of FIR filter taps (must be odd and >= 3).
     */
    explicit Hilbert(int taps = DEFAULT_TAPS);

    /**
     * @brief Get the group delay in samples ((taps - 1) / 2).
     * @return Group delay.
     */
    [[nodiscard]] int delay() const;

    /**
     * @brief Get the number of filter taps.
     * @return Tap count.
     */
    [[nodiscard]] int taps() const;

    /**
     * @brief Process a single input sample and produce an analytic signal pair.
     * @param in Real-valued input sample.
     * @return IqSample with delayed I and Hilbert-transformed Q.
     */
    [[nodiscard]] IqSample process(float in);

private:
    int taps_;                        ///< Number of FIR filter taps.
    int delay_;                       ///< Group delay in samples.
    std::vector<float> coeffs_;       ///< Hilbert FIR coefficients.
    std::vector<float> hilbertLine_;  ///< Circular buffer for Q FIR convolution.
    std::vector<float> delayLine_;    ///< Circular buffer for I delay line.
    int pos_{};                       ///< Current write position in circular buffers.
};
