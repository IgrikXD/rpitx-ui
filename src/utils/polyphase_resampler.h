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

#include <algorithm>
#include <cstddef>
#include <numeric>
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

// --------------------------------------------------------------------------
// Rate-conversion helpers
// --------------------------------------------------------------------------
// Free, side-effect-free helpers used to derive polyphase parameters from
// source / target sample rates and a desired block size. Kept exposed (not
// hidden inside AudioRateConverter) because the L/M math is independently
// useful for callers that need finer control than the high-level converter
// offers - e.g. multi-channel processors that want a single L/M pair shared
// across several PolyphaseResampler instances.
// --------------------------------------------------------------------------

/**
 * @brief Polyphase factor pair (interpolation L, decimation M) for L/M rate conversion.
 *
 * Produced by computePolyphaseRatio(); consumed by alignedInputForOutput()
 * and by PolyphaseResampler's constructor.
 */
struct PolyphaseRatio {
    int L;  ///< Interpolation factor (== targetRate / gcd(srcRate, tgtRate)).
    int M;  ///< Decimation factor    (== sourceRate / gcd(srcRate, tgtRate)).
};

/**
 * @brief Reduce source / target sample rates to coprime L/M factors via gcd.
 *
 * The gcd reduction is the tightest possible, yielding the smallest L
 * (== phase count materialised by the resampler). Common rates produce
 * modest L:
 *   48000 -> 228000 :  L=19,   M=4
 *   44100 -> 48000  :  L=160,  M=147
 *   22050 -> 48000  :  L=320,  M=147
 *   96000 -> 48000  :  L=1,    M=2
 *
 * @param sourceRate Input sample rate in Hz (must be > 0).
 * @param targetRate Output sample rate in Hz (must be > 0).
 * @return Coprime L/M pair such that targetRate / sourceRate == L / M.
 */
[[nodiscard]] constexpr PolyphaseRatio computePolyphaseRatio(int sourceRate, int targetRate) {
    const int g{std::gcd(sourceRate, targetRate)};
    return PolyphaseRatio{.L = targetRate / g, .M = sourceRate / g};
}

/**
 * @brief Smallest input block size (multiple of M) producing >= targetOutput
 *        output frames.
 *
 * Use when the latency budget is fixed at the OUTPUT rate - typical for
 * piam / pinfm where the DMA cadence at 48 kHz governs end-to-end delay.
 *
 * @param targetOutput Minimum desired output frames per block (> 0).
 * @param L Polyphase interpolation factor (>= 1).
 * @param M Polyphase decimation factor (>= 1).
 * @return Input frame count, always a multiple of M, that yields at least
 *         targetOutput output frames after L/M conversion.
 */
[[nodiscard]] constexpr int alignedInputForOutput(int targetOutput, int L, int M) {
    // ceil(targetOutput * M / L), then round up to the next multiple of M.
    const int approxIn{(targetOutput * M + L - 1) / L};
    return ((approxIn + M - 1) / M) * M;
}

/**
 * @brief Safe LPF cutoff for a polyphase resampler given source / target rates.
 *
 * Caps the requested maxCutoffHz at 0.45 * min(sourceRate, targetRate) -
 * the 0.45 factor leaves a 10 % margin below the half-Nyquist point of
 * whichever rate is smaller, which is what the prototype filter has to
 * sit under to avoid aliasing on either side of the conversion. Callers
 * pass their natural audio-band LPF (e.g. 4500 Hz for AM, 3000 Hz for
 * NBFM, 15000 Hz for FM) and let this helper trim it down only when the
 * source rate is too low to support that bandwidth.
 *
 * @param sourceRate Input sample rate in Hz (> 0).
 * @param targetRate Output sample rate in Hz (> 0).
 * @param maxCutoffHz Caller's preferred maximum cutoff (> 0).
 * @return Effective cutoff in Hz, guaranteed to satisfy the resampler's
 *         cutoff < min(srcRate, tgtRate) / 2 invariant.
 */
[[nodiscard]] constexpr float safeResamplerCutoff(int sourceRate, int targetRate, float maxCutoffHz) {
    const float minRate{static_cast<float>(std::min(sourceRate, targetRate))};
    return std::min(maxCutoffHz, 0.45F * minRate);
}

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
     *                           converter caps this via safeResamplerCutoff
     *                           so the prototype filter never aliases on
     *                           either side. Ignored in passthrough mode.
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
     * @brief Whether the converter operates in passthrough (copy) mode.
     *
     * True iff source rate == target rate at construction; in that case
     * process() performs a plain copy with no resampler involvement.
     */
    [[nodiscard]] bool isPassthrough() const;

    /**
     * @brief Process one block of audio.
     *
     * In resample mode, runs the input through the polyphase resampler.
     * In passthrough mode, copies in -> out (sizes must match by contract).
     *
     * @pre in.size() == inputFrames()
     * @pre out.size() == outputFrames()
     */
    void process(std::span<const float> in, std::span<float> out);

private:
    int inputFrames_;
    int outputFrames_;
    std::optional<PolyphaseResampler> resampler_;  ///< nullopt = passthrough.
};
