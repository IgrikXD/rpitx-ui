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
#include <optional>
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
 * (L * sampleRateIn) rate. Each polyphase sub-filter is normalised to unity
 * DC gain after the +L zero-stuffing compensation. cutoffHz must sit safely
 * below min(Fs_in, Fs_out) / 2 to avoid aliasing on either side.
 *
 * @code
 * // 48 kHz -> 228 kHz (L=19, M=4) with 32 taps per phase, 15 kHz LPF
 * PolyphaseResampler r{19, 4, 32, 15'000.0F, 48'000.0F};
 * std::array<float, 1024>  in{};
 * std::array<float, 4'864> out{};   // 1024 * 19 / 4 = 4864
 * const bool ok{r.resample(in, out)};
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

    // Non-copyable: the polyphase delay line carries DSP history; duplicating
    // it mid-stream would fork the filter state. Move is allowed so the type
    // can be returned from factory functions and held by std::optional via
    // assignment - move-construct transfers the delay line, move-assign
    // overwrites it (use the latter only to replace a finished converter,
    // never mid-stream, since it discards the previous filter history).
    PolyphaseResampler(const PolyphaseResampler&)            = delete;
    PolyphaseResampler& operator=(const PolyphaseResampler&) = delete;
    PolyphaseResampler(PolyphaseResampler&&)                 = default;
    PolyphaseResampler& operator=(PolyphaseResampler&&)      = default;

    /**
     * @brief Process one block of input samples, writing the resampled output.
     *
     * The contract guarantees deterministic block-to-block alignment: with the
     * input size a multiple of M, the internal phase state advances by an
     * integer number of full L-cycles and so re-enters every block in the
     * same configuration it left the previous one - no carry samples, no
     * partial outputs. Invalid buffer sizes are rejected before the delay
     * line or phase state are changed.
     *
     * @param in  Input samples (size must be a multiple of M).
     * @param out Output buffer (size must equal in.size() * L / M).
     * @return true on success, false when the buffer geometry is invalid.
     */
    [[nodiscard]] bool resample(std::span<const float> in, std::span<float> out);

private:
    [[nodiscard]] std::optional<std::size_t> outputSize(std::size_t inSize) const;

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

/**
 * @brief High-level streaming rate converter wrapping PolyphaseResampler.
 *
 * Encapsulates the L/M derivation, block-size alignment, safe cutoff
 * selection, and passthrough optimisation: when sourceRate == targetRate,
 * no resampler is instantiated and process() degrades to a memory copy.
 * Callers see a uniform process() interface regardless and do not have
 * to write `if (needsResample) ... else ...` branches in their audio
 * loops.
 *
 * Block-size policy is fixed: the caller's targetOutputFrames is the
 * minimum number of output samples produced per process() call. The
 * input block size is then derived as the smallest M-multiple that
 * yields at least that many outputs. This single policy fits every
 * consumer in the project (piam / pinfm at 48 kHz output, pifmrds at
 * 228 kHz output - latency in seconds varies but the API does not).
 *
 * Non-copyable; movable so the converter can be assigned into
 * std::optional members in deferred-construction patterns (e.g.
 * processors that learn the source rate at run time).
 */
class AudioRateConverter {
public:
    /**
     * @brief Construct an AudioRateConverter for the given rate pair.
     *
     * @param sourceRate         Input sample rate in Hz (> 0).
     * @param targetRate         Output sample rate in Hz (> 0).
     * @param targetOutputFrames Minimum desired output frames per block (> 0).
     *                           Sets the latency budget at the output rate;
     *                           in seconds this is targetOutputFrames /
     *                           targetRate.
     * @param tapsPerPhase       Resampler taps per phase (ignored in
     *                           passthrough mode).
     * @param maxCutoffHz        Caller's preferred LPF cutoff in Hz. The
     *                           converter caps this internally so the
     *                           prototype filter never aliases on either
     *                           side. Ignored in passthrough mode.
     */
    AudioRateConverter(int sourceRate, int targetRate, int targetOutputFrames, int tapsPerPhase, float maxCutoffHz);

    AudioRateConverter(const AudioRateConverter&)            = delete;
    AudioRateConverter& operator=(const AudioRateConverter&) = delete;
    AudioRateConverter(AudioRateConverter&&)                 = default;
    AudioRateConverter& operator=(AudioRateConverter&&)      = default;

    /**
     * @brief Required input frames per process() call.
     *
     * Always a multiple of the polyphase decimation factor M when the
     * converter is in resample mode; equals targetOutputFrames in
     * passthrough mode.
     */
    [[nodiscard]] int inputFrames() const;

    /**
     * @brief Output frames produced per process() call.
     *
     * Equals inputFrames() * L / M in resample mode; equals inputFrames()
     * in passthrough mode.
     */
    [[nodiscard]] int outputFrames() const;

    /**
     * @brief Process one block of audio.
     *
     * In resample mode, runs the input through the polyphase resampler.
     * In passthrough mode, copies in -> out (sizes must match by contract).
     *
     * @return true on success, false when the buffer geometry is invalid.
     */
    [[nodiscard]] bool process(std::span<const float> in, std::span<float> out);

private:
    int inputFrames_;
    int outputFrames_;
    std::optional<PolyphaseResampler> resampler_;  ///< nullopt = passthrough.
};
