/**
 * @file nfm_processor.h
 * @brief Narrow-band FM (NBFM) frequency-deviation processor.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 24.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <array>

#include "agc.h"
#include "biquad.h"

/**
 * @brief Streaming NBFM modulation processor producing per-sample frequency offsets in Hz.
 *
 * Processes normalized float audio into a frequency-deviation stream suitable
 * for direct consumption by librpitx::ngfmdmasync::SetFrequencySamples(). The
 * output is the instantaneous carrier offset in Hz - positive or negative,
 * peak-clipped at the configured peak deviation - which the DMA back-end
 * translates into a PLL fractional-divisor delta every sample.
 *
 * DSP chain: HPF 30 Hz (DC block) -> 4th-order Butterworth LPF 3000 Hz
 * (voice-NBFM bandwidth guard) -> scalar AGC -> peak-deviation scaling with
 * hard clamp.
 *
 * @code
 * NfmProcessor nfm{48'000.0f, 5'000.0f};
 * const float offsetHz{nfm.process(audioSample)};
 * @endcode
 */
class NfmProcessor {
public:
    /**
     * @brief Construct an NFM processor for the given audio sample rate and peak deviation.
     *
     * The sample rate is used to design the HPF/LPF coefficients; it must
     * match the rate at which process() is subsequently called. The peak
     * deviation sets the output clamp bound and the post-AGC scale factor.
     *
     * @param sampleRate Audio sample rate in Hz.
     * @param peakDeviation Peak frequency deviation in Hz; output is clamped to [-peakDeviation, +peakDeviation].
     */
    NfmProcessor(float sampleRate, float peakDeviation);

    // Non-copyable, non-movable: the processor owns IIR state (Biquad delay
    // lines and Agc envelope estimate) that is meaningful only in place -
    // duplicating it mid-stream would fork the DSP history, and moving it
    // after construction is not a pattern any current caller needs.
    NfmProcessor(const NfmProcessor&)            = delete;
    NfmProcessor& operator=(const NfmProcessor&) = delete;
    NfmProcessor(NfmProcessor&&)                 = delete;
    NfmProcessor& operator=(NfmProcessor&&)      = delete;

    /**
     * @brief Process a single normalized audio sample into an NBFM frequency offset.
     *
     * The AGC stage normalises whatever it receives, so the input magnitude
     * is a convention (typically PCM16 scaled to [-1, 1]) rather than a
     * hard precondition - larger or smaller values are renormalised, not rejected.
     *
     * @param sample Input audio sample (normalised float, conventionally in [-1, 1]).
     * @return Instantaneous frequency offset in Hz, clamped to [-peakDeviation, +peakDeviation].
     */
    [[nodiscard]] float process(float sample);

private:
    /**
     * @brief High-pass cutoff frequency in Hz (removes DC and sub-audio rumble).
     *
     * A residual DC offset in the audio would bias the instantaneous carrier
     * frequency (a constant deviation is an unwanted static carrier shift),
     * so a gentle HPF is applied before AGC.
     */
    static constexpr float HPF_CUTOFF{30.0f};

    /**
     * @brief Low-pass cutoff frequency in Hz (voice-NBFM audio bandwidth guard).
     *
     * Matches the 3 kHz voice bandwidth used by ITU-R narrow-band FM voice
     * channels. Audio energy above this point would, via Carson's rule,
     * widen the occupied RF bandwidth beyond the channel mask and splatter
     * onto adjacent allocations.
     */
    static constexpr float LPF_CUTOFF{3'000.0f};

    /**
     * @brief Order of the cascaded Butterworth low-pass filter.
     *
     * A 4th-order Butterworth LPF rolls off at -24 dB/oct (~-48 dB two
     * octaves above cutoff). The 2nd-order section used before this rewrite
     * (-12 dB/oct, ~-24 dB two octaves above) left audible voice energy
     * above 6 kHz; once the peak deviation is allowed to reach 5 kHz,
     * Carson's rule expands the occupied RF bandwidth to ~16 kHz and that
     * residual energy starts splattering into adjacent 25 kHz channels.
     * 4th order restores the ~40 dB channel-mask margin needed on wide-mode
     * NBFM and is realised as LPF_ORDER / 2 cascaded biquad sections.
     */
    static constexpr int LPF_ORDER{4};

    /**
     * @brief AGC configuration tuned for NBFM voice transmission.
     *
     * Identical timing to piam's broadcast-style leveller: AGC control
     * envelope cutoff sits below the lowest voice fundamental (~85 Hz for
     * male speech), so gain modulation within a glottal period cannot
     * produce intermodulation that would widen the NBFM spectrum. Over-
     * deviation on NBFM radiates straight into adjacent channels, so
     * keeping the AGC out of the audio band is a spectral-containment
     * requirement, not just a fidelity choice. At 48 kHz the coefficients
     * translate to:
     *   attack = 0.003   -> tau ~= 6.9 ms  (control cutoff ~23 Hz)
     *   decay  = 0.0001  -> tau ~= 208 ms  (classic broadcast release)
     * target 0.8 leaves headroom for a transient peak to reach the hard
     * clamp at +-1 (i.e. +-peakDeviation) without producing sustained
     * over-deviation; initialEnvelope is seeded at target so the first
     * sample sees gain = 1.0 and stays within the clamp - a small seed
     * would saturate the opening milliseconds into a +-peakDeviation
     * square wave (audible thump + broad spectral content).
     */
    static constexpr AgcConfig NFM_AGC_CONFIG{
        .target          = 0.8f,
        .attack          = 0.003f,
        .decay           = 0.0001f,
        .initialEnvelope = 0.8f,
    };

    Biquad hpf_;                             ///< DC block.
    std::array<Biquad, LPF_ORDER / 2> lpfChain_;  ///< Butterworth LPF realised as LPF_ORDER / 2 cascaded biquads.
    Agc agc_{NFM_AGC_CONFIG};                ///< Scalar automatic gain control.
    float peakDeviation_;                    ///< Output clamp bound and post-AGC scale factor, in Hz.
};
