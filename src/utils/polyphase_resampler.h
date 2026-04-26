/**
 * @file polyphase_resampler.h
 * @brief Rational L/M polyphase FIR resampler for fixed sample-rate conversions.
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
#include <vector>

/**
 * @brief Streaming rational-rate FIR resampler (Fs_in -> Fs_out = Fs_in * L / M).
 *
 * Implements the classical polyphase decomposition: a single Hamming-windowed
 * sinc prototype filter of L * tapsPerPhase taps, computed once at the
 * virtual L*Fs_in rate, is split into L sub-filters of tapsPerPhase taps
 * each. Each output sample picks one sub-filter according to the phase
 * accumulator and convolves it against the input delay line. Decimation
 * is implicit: only every M-th virtual output is computed.
 *
 * The block-mode entry point is the only one exposed: callers feed input
 * samples in groups whose size is a multiple of M, and receive exactly
 * inSize * L / M output samples per call. Forcing the block-size invariant
 * keeps the per-block phase state self-consistent (no cross-block leftover),
 * which is the simplest contract for downstream pipelines.
 *
 * The prototype filter is designed inside the constructor as a
 * Hamming-windowed sinc with cutoff at cutoffHz, evaluated at the virtual
 * (L * sampleRateIn) rate, with a +L gain to compensate for the implicit
 * zero-stuffing of the polyphase decomposition. cutoffHz must sit safely
 * below min(Fs_in, Fs_out) / 2 to avoid aliasing on either side.
 *
 * @code
 * // 48 kHz -> 228 kHz (L=19, M=4) with 32 taps per phase, 15 kHz LPF
 * PolyphaseResampler r{19, 4, 32, 15'000.0F, 48'000.0F};
 * std::array<float, 1024>  in{};
 * std::array<float, 4'864> out{};   // 1024 * 19 / 4 = 4864
 * r.resample(in, out);
 * @endcode
 */
class PolyphaseResampler {
public:
    /**
     * @brief Construct a polyphase resampler for L/M rate conversion.
     *
     * @param interpL       Interpolation factor (must be >= 1).
     * @param decimM        Decimation factor (must be >= 1).
     * @param tapsPerPhase  Number of taps per polyphase sub-filter (>= 4 typical).
     * @param cutoffHz      Low-pass cutoff in Hz; must be < min(Fs_in, Fs_out) / 2.
     * @param sampleRateIn  Input sample rate in Hz; used only to design the
     *                      prototype filter (no run-time rate tracking).
     */
    PolyphaseResampler(int interpL, int decimM, int tapsPerPhase, float cutoffHz, float sampleRateIn);

    PolyphaseResampler(const PolyphaseResampler&)            = delete;
    PolyphaseResampler& operator=(const PolyphaseResampler&) = delete;
    PolyphaseResampler(PolyphaseResampler&&)                 = delete;
    PolyphaseResampler& operator=(PolyphaseResampler&&)      = delete;

    /**
     * @brief Process one block of input samples, writing the resampled output.
     *
     * @pre in.size() is a multiple of M.
     * @pre out.size() == in.size() * L / M.
     *
     * The contract guarantees deterministic block-to-block alignment: with the
     * input size a multiple of M, the internal phase state advances by an
     * integer number of full L-cycles and so re-enters every block in the
     * same configuration it left the previous one - no carry samples, no
     * partial outputs.
     *
     * @param in  Input samples (size must be a multiple of M).
     * @param out Output buffer (size must equal in.size() * L / M).
     */
    void resample(std::span<const float> in, std::span<float> out);

    /**
     * @brief Number of output samples produced for a given input block size.
     *
     * Convenience helper that mirrors the resample() size invariant so callers
     * can size their output buffers without recomputing the L/M ratio at
     * each call site.
     *
     * @param inSize Input block size (must be a multiple of M).
     * @return inSize * L / M.
     */
    [[nodiscard]] std::size_t outputSize(std::size_t inSize) const;

private:
    /**
     * @brief Push a new input sample into the delay line.
     *
     * The delay line is stored newest-first (delay_[0] is the most recent
     * input), which matches the natural indexing in convolveAtPhase().
     *
     * @param sample New input sample.
     */
    void pushDelay(float sample);

    /**
     * @brief Convolve the current delay line with a polyphase sub-filter.
     * @param phase Polyphase index in [0, L).
     * @return Filtered output sample.
     */
    [[nodiscard]] float convolveAtPhase(int phase) const;

    int L_;             ///< Interpolation factor.
    int M_;             ///< Decimation factor.
    int tapsPerPhase_;  ///< Number of taps per polyphase sub-filter.

    /**
     * @brief Polyphase coefficients laid out as [phase][tap].
     *
     * Stored as a flat std::vector with row stride tapsPerPhase_ to keep the
     * inner convolution loop cache-friendly (one phase row read sequentially
     * per output sample).
     */
    std::vector<float> coefs_;

    std::vector<float> delay_;  ///< Delay line, newest-first.

    /**
     * @brief Virtual phase tracker, equal to (consumed_inputs * L) mod M.
     *
     * For a block whose size is a multiple of M, this returns to the
     * post-construction value of 0 at the end of every call - so it is
     * essentially block-invariant, but the field is kept for the
     * intra-block iteration.
     */
    int virtualPhase_{0};
};
