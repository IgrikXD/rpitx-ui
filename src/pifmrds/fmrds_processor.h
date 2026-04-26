/**
 * @file fmrds_processor.h
 * @brief FM broadcast MPX processor with embedded RDS subcarrier and optional
 *        stereo (pilot + 38 kHz suppressed-carrier L-R) generation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 26.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <optional>
#include <span>

#include "agc.h"
#include "biquad.h"
#include "polyphase_resampler.h"
#include "rds_encoder.h"
#include "rds_modulator.h"

/**
 * @brief Configuration for FmRdsProcessor.
 *
 * @attention All fields must be set explicitly - no in-class initializers.
 */
struct FmRdsConfig {
    int audioSampleRate;   ///< Input audio sample rate in Hz (any reasonable PCM rate).
    int channels;          ///< Channel count: 1 (mono) or 2 (stereo).
    int mpxSampleRate;     ///< MPX/DMA sample rate in Hz (228000 - locked to 4 x 57 kHz).
    float peakDeviation;   ///< FM peak deviation in Hz (75000 for FM broadcast).
    float preEmphasisTau;  ///< Pre-emphasis time constant in seconds (50e-6 or 75e-6).
};

/**
 * @brief Streaming MPX-domain processor that converts audio + RDS into
 *        per-sample frequency-deviation values for ngfmdmasync.
 *
 * Audio chain (per input channel, applied at the audio sample rate):
 *   1. HPF 30 Hz   - DC block (a residual DC offset would manifest as a
 *                    constant carrier shift that wastes deviation headroom).
 *   2. Pre-emphasis (50 / 75 us) - matches the receiver's de-emphasis so
 *                    the noise floor at the detector output stays flat.
 *   3. LPF 15 kHz  - FM broadcast audio mask. Energy above 15 kHz would
 *                    spill into the 19 kHz pilot band and break stereo
 *                    receivers; even mono receivers benefit from the
 *                    out-of-band suppression.
 *
 * Mono path uses the scalar AGC overload directly. Stereo uses the
 * IqSample overload (L as I, R as Q), which tracks sqrt(L^2 + R^2) and
 * applies the same gain to both channels - the stereo image stays intact
 * because L and R never see independent gain trajectories.
 *
 * MPX chain (at the 228 kHz MPX rate):
 *   4. Polyphase resampling per channel (rational L/M derived from
 *      gcd(audioSampleRate, mpxSampleRate)).
 *   5. Compose MPX:
 *        - mono:    audio + RDS_subcarrier
 *        - stereo:  (L+R) + pilot_19k + (L-R) * sin(2 pi 38k t) + RDS_subcarrier
 *      Phase-locked 19 kHz pilot and 38 kHz subcarrier (38 kHz = 2 * 19 kHz)
 *      use sine LUTs sized to the integer 228 kHz / pilot_freq ratios
 *      (12 phases for 19 kHz, 6 phases for 38 kHz) so no NCO arithmetic
 *      is needed at run time.
 *   6. Hard-clamp to +-peakDeviation - the AGC can briefly overshoot on a
 *      level transient and an unclamped overshoot would radiate as
 *      adjacent-channel splatter.
 *
 * @code
 * FmRdsProcessor proc{{
 *     .audioSampleRate = 44100,
 *     .channels        = 2,
 *     .mpxSampleRate   = 228000,
 *     .peakDeviation   = 75000.0F,
 *     .preEmphasisTau  = 50e-6F,
 * }};
 * proc.encoder().setPi(0xFFFF);
 * std::vector<float> audioInLR(proc.audioFramesPerBlock() * 2);
 * std::vector<float> mpxOut(proc.mpxSamplesPerBlock());
 * proc.process(audioInLR, mpxOut);
 * @endcode
 */
class FmRdsProcessor {
public:
    explicit FmRdsProcessor(const FmRdsConfig& config);

    FmRdsProcessor(const FmRdsProcessor&)            = delete;
    FmRdsProcessor& operator=(const FmRdsProcessor&) = delete;
    FmRdsProcessor(FmRdsProcessor&&)                 = delete;
    FmRdsProcessor& operator=(FmRdsProcessor&&)      = delete;

    /**
     * @brief Access the underlying RDS encoder (PI/PS/RT/TA setters).
     *
     * The processor owns the RdsModulator which in turn owns the
     * RdsEncoder; this two-step accessor lets main() reach the encoder
     * without exposing the modulator.
     *
     * @return Reference to the owned RDS encoder.
     */
    [[nodiscard]] RdsEncoder& encoder();

    /**
     * @brief Audio frames per processing block.
     *
     * The polyphase resampler requires the input frame count per call to
     * be a multiple of the decimation factor M (which depends on the
     * input/MPX rate ratio). audioFramesPerBlock() returns a target block
     * size that satisfies that invariant and lands close to a 20 ms audio
     * window (the same ballpark piam / pinfm use). Callers must respect
     * this size when building their input buffers.
     *
     * @return Frame count per process() call (always a multiple of M).
     */
    [[nodiscard]] int audioFramesPerBlock() const;

    /**
     * @brief MPX samples produced per process() block.
     *
     * @return Output sample count per process() call (== frames * L / M).
     */
    [[nodiscard]] int mpxSamplesPerBlock() const;

    /**
     * @brief Process one block of audio into MPX-domain frequency deviations.
     *
     * @pre For mono:   audioIn.size() == audioFramesPerBlock().
     * @pre For stereo: audioIn.size() == audioFramesPerBlock() * 2,
     *                  interleaved as L0, R0, L1, R1, ...
     * @pre mpxOut.size() == mpxSamplesPerBlock().
     *
     * @param audioIn Normalised audio samples in [-1, 1] (PCM16 / 32768 typical).
     *                Mono: single channel. Stereo: interleaved L, R.
     * @param mpxOut  Output buffer, written with one frequency deviation per
     *                MPX sample (Hz, clamped to +-peakDeviation).
     */
    void process(std::span<const float> audioIn, std::span<float> mpxOut);

private:
    /**
     * @brief Audio-rate AGC tuning, identical to piam/pinfm.
     *
     * Same broadcast-leveller timing as the AM and NBFM processors:
     * attack=0.003 (~7 ms tau, ~23 Hz control cutoff) keeps the AGC
     * response well below the lowest voice fundamental (~85 Hz), so
     * gain modulation within a glottal period cannot produce IM that
     * would widen the MPX spectrum past the LPF guard. decay=0.0001
     * (~208 ms) gives the classic broadcast release that avoids
     * inter-syllable pumping. initialEnvelope=target so the first
     * sample sees gain=1.0 and stays inside the deviation clamp.
     */
    static constexpr AgcConfig FMRDS_AGC_CONFIG{
        .target          = 0.8F,
        .attack          = 0.003F,
        .decay           = 0.0001F,
        .initialEnvelope = 0.8F,
    };

    /**
     * @brief Audio HPF cutoff (DC blocker), in Hz.
     */
    static constexpr float HPF_CUTOFF{30.0F};

    /**
     * @brief Audio LPF cutoff: FM broadcast mask, in Hz.
     *
     * EN 50067 / ITU-R BS.450 cap the audio bandwidth of an FM broadcast
     * MPX at 15 kHz so the 19 kHz stereo pilot region stays clean.
     */
    static constexpr float LPF_CUTOFF{15'000.0F};

    /**
     * @brief Number of cascaded biquad sections for the 4th-order LPF.
     *
     * 4th-order Butterworth (-24 dB/oct) gives ~40 dB attenuation an
     * octave above 15 kHz, which keeps audio energy out of both the
     * 19 kHz pilot region and the 38 kHz stereo subcarrier.
     */
    static constexpr int LPF_ORDER{4};

    /**
     * @brief Polyphase resampler taps per phase.
     *
     * 32 taps per phase combined with a Hamming window gives ~80 dB of
     * stop-band attenuation, sufficient to keep image spectra of the audio
     * from spilling near the 19 kHz pilot and the 38 kHz subcarrier.
     */
    static constexpr int RESAMPLER_TAPS_PER_PHASE{32};

    /**
     * @brief Per-channel audio gain into the L+R sum and L-R difference.
     *
     * Worked through the AGC target (0.8) the gain budget breaks down to:
     *   - mono:                   2 * 0.45 * 0.8 + 0.027 (RDS) ~= 0.75
     *                                                 -> ~56 kHz peak deviation
     *   - stereo (any L/R mix):   0.45 * 1.132 + 0.10 (pilot) + 0.027 (RDS) ~= 0.64
     *                                                 -> ~48 kHz peak deviation
     * Both stay comfortably below the 75 kHz EN 50067 cap, leaving headroom
     * for AGC overshoot transients without driving the hard +-1 clamp.
     * Stereo lands quieter than mono by design - the deviation budget split
     * with the pilot and the L-R subcarrier mirrors what PiFmRds upstream
     * does, so receivers tuned against that reference see comparable level
     * statistics.
     */
    static constexpr float AUDIO_SUM_GAIN{0.45F};

    /**
     * @brief 19 kHz pilot tone level, fraction of peak deviation.
     *
     * EN 50067 / ITU-R BS.450 specify the pilot at 8-10 % of peak
     * deviation. 10 % matches the upstream PiFmRds reference and keeps
     * stereo lock margin generous on inexpensive receivers.
     */
    static constexpr float PILOT_GAIN{0.10F};

    /**
     * @brief RDS subcarrier level, fraction of peak deviation.
     *
     * 5 % nominal yields ~3.75 kHz peak RDS deviation at a 75 kHz peak
     * total - the canonical EN 50067 "high pilot level" preset, which is
     * preferred for noisy reception conditions.
     */
    static constexpr float RDS_GAIN{0.05F};

    /**
     * @brief 19 kHz pilot phase period in 228 kHz samples (228 / 19 = 12).
     */
    static constexpr int PILOT_19K_PERIOD{12};

    /**
     * @brief 38 kHz stereo-subcarrier phase period in 228 kHz samples (228 / 38 = 6).
     *
     * Phase-locked to the 19 kHz pilot at every period boundary because
     * 12 / 6 = 2 - any receiver that recovers the pilot at the right
     * polarity automatically demodulates the L-R subcarrier coherently.
     */
    static constexpr int CARRIER_38K_PERIOD{6};

    /**
     * @brief Per-channel audio filter chain (HPF -> pre-emph -> LPF cascade).
     *
     * Bundled into a struct so the stereo case can hold two of them in a
     * std::array without repeating the per-section state at the processor
     * level. The LPF is realised as LPF_ORDER / 2 cascaded biquad sections
     * with staggered Q values (see Butterworth-cascade derivation in the
     * .cpp), giving the full 4th-order response at 15 kHz.
     */
    struct ChannelFilters {
        Biquad hpf;
        Biquad preEmphasis;
        std::array<Biquad, LPF_ORDER / 2> lpfChain;

        /**
         * @brief Apply the full HPF -> pre-emph -> LPF chain to one sample.
         */
        [[nodiscard]] float process(float x);
    };

    /**
     * @brief Build one configured ChannelFilters instance.
     *
     * Used by the constructor's init list to populate two channels without
     * repeating the same Biquad factory calls per channel.
     *
     * @param config Source FmRdsConfig.
     * @return Channel filter bundle initialised against the config's audio
     *         sample rate and pre-emphasis time constant.
     */
    [[nodiscard]] static ChannelFilters makeChannelFilters(const FmRdsConfig& config);

    /**
     * @brief Process one audio frame through the channel filters and AGC.
     *
     * Mono branches into the scalar Agc::process(float); stereo uses the
     * IqSample overload to apply a single envelope-tracking gain to both
     * channels. Returns the filtered, gain-adjusted, clamped audio for
     * both channels - the .q field always carries a valid value (set to
     * the L sample in mono) so the downstream stereo MPX path can read
     * both buffers without a special case.
     *
     * @param l Left-channel input sample.
     * @param r Right-channel input sample (ignored when channels == 1).
     * @return Pair of post-AGC samples (.i = L, .q = R or copy of L).
     */
    [[nodiscard]] IqSample preprocessFrame(float l, float r);

    /**
     * @brief Build the MPX sample for one 228 kHz tick.
     *
     * Composes the audio sum with the pilot, the L-R subcarrier (stereo
     * only), and the RDS subcarrier; advances the pilot/subcarrier phase
     * counters; clamps to +-1 in normalised units before scaling to Hz.
     *
     * @param l Resampled left audio sample at 228 kHz.
     * @param r Resampled right audio sample at 228 kHz (== L for mono).
     * @return Frequency deviation in Hz, clamped to +-peakDeviation.
     */
    [[nodiscard]] float buildMpxSample(float l, float r);

    int channels_;
    float peakDeviation_;

    /**
     * @brief Per-channel audio filters; lpfChain shape is fixed at compile time.
     *
     * Always sized 2; in mono mode the second slot is unused (so we pay a
     * small upfront cost to avoid a polymorphic / pointer-indirected design
     * for what is otherwise a trivial container).
     */
    std::array<ChannelFilters, 2> filters_;

    /**
     * @brief Audio AGC. Used in two modes:
     *   - Mono: scalar overload Agc::process(float) on the single channel.
     *   - Stereo: IqSample overload (L as I, R as Q), which tracks
     *     sqrt(L^2 + R^2) and applies the same gain to both components,
     *     preserving the stereo image while normalising joint loudness.
     */
    Agc agc_{FMRDS_AGC_CONFIG};

    /**
     * @brief Per-channel polyphase resampler (audio rate -> 228 kHz).
     *
     * Held in optional slots because PolyphaseResampler is non-movable, and
     * the constructor computes L/M from the configured rates before
     * constructing it in place. Two slots: in mono mode the second slot
     * remains disengaged.
     */
    std::array<std::optional<PolyphaseResampler>, 2> resamplers_;

    RdsModulator rdsModulator_;

    int audioFrames_{0};  ///< Frames per process() block (multiple of M).
    int mpxSamples_{0};   ///< MPX samples per process() block (== frames * L / M).

    /**
     * @brief 19 kHz pilot phase index in [0, 12). Advances by 1 per MPX sample.
     */
    int pilotPhase_{0};

    /**
     * @brief 38 kHz subcarrier phase index in [0, 6). Advances by 1 per MPX sample.
     */
    int carrierPhase_{0};

    /**
     * @brief Pre-computed sin(2 pi * 19 kHz * t) sampled at 228 kHz, length 12.
     *
     * Initialised in the .cpp - constexpr sin is C++26, and computing it
     * once at construction time costs 12 sines, well under a microsecond.
     */
    std::array<float, PILOT_19K_PERIOD> pilotTable_{};

    /**
     * @brief Pre-computed sin(2 pi * 38 kHz * t) sampled at 228 kHz, length 6.
     */
    std::array<float, CARRIER_38K_PERIOD> carrierTable_{};

    /**
     * @brief Audio-domain scratch buffers (per channel).
     *
     * Sized by audioFrames_ in the ctor. Holds the post-filter, post-AGC
     * float samples for one block before they are fed into the per-channel
     * resampler.
     */
    std::array<std::vector<float>, 2> audioScratch_;

    /**
     * @brief MPX-domain scratch buffers (per channel).
     *
     * Sized by mpxSamples_ in the ctor. Holds the per-channel resampled
     * 228 kHz audio before the L+R / L-R / pilot / RDS mixer combines
     * them in buildMpxSample().
     */
    std::array<std::vector<float>, 2> mpxScratch_;
};
