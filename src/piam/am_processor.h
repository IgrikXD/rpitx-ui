/**
 * @file am_processor.h
 * @brief Amplitude-modulation (AM) envelope processor.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 20.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include "agc.h"
#include "biquad.h"

/**
 * @brief Streaming DSB-FC AM envelope processor producing a normalized envelope stream.
 *
 * Processes normalized float audio into an amplitude envelope suitable for
 * direct consumption by librpitx::amdmasync::SetAmSamples(). The output is the
 * canonical AM envelope s = 0.5 * (1 + m * a(t)) with modulation depth m fixed
 * at MODULATION_DEPTH; the DC-shifted form keeps the signal in [0, 1] so the
 * amdmasync pad-drive quantizer receives only non-negative amplitudes.
 *
 * DSP chain: HPF 30 Hz (DC block) -> LPF 4500 Hz (voice-AM bandwidth
 * guard) -> scalar AGC -> 0.5 * (1 + m * a) envelope formation.
 *
 * @code
 * AmProcessor am{48'000.0f};
 * const float envelope{am.process(audioSample)};
 * @endcode
 */
class AmProcessor {
public:
    /**
     * @brief Construct an AM processor for the given audio sample rate.
     *
     * The sample rate is used to design the HPF/LPF coefficients; it must
     * match the rate at which process() is subsequently called.
     *
     * @param sampleRate Audio sample rate in Hz.
     */
    explicit AmProcessor(float sampleRate);

    // Non-copyable, non-movable: the processor owns IIR state (Biquad delay
    // lines and Agc envelope estimate) that is meaningful only in place -
    // duplicating it mid-stream would fork the DSP history, and moving it
    // after construction is not a pattern any current caller needs.
    AmProcessor(const AmProcessor&)            = delete;
    AmProcessor& operator=(const AmProcessor&) = delete;
    AmProcessor(AmProcessor&&)                 = delete;
    AmProcessor& operator=(AmProcessor&&)      = delete;

    /**
     * @brief Process a single normalized audio sample into an AM envelope.
     *
     * The AGC stage normalises whatever it receives, so the input magnitude
     * is a convention (typically PCM16 scaled to [-1, 1]) rather than a
     * hard precondition - larger or smaller values are renormalised, not rejected.
     *
     * @param sample Input audio sample (normalised float, conventionally in [-1, 1]).
     * @return AM envelope in [0.0, 1.0] (clamped for safety).
     */
    [[nodiscard]] float process(float sample);

private:
    /**
     * @brief High-pass cutoff frequency in Hz (removes DC and sub-audio rumble).
     *
     * A residual DC offset in the audio would bias the envelope asymmetrically
     * and waste modulation headroom, so a gentle HPF is applied before AGC.
     */
    static constexpr float HPF_CUTOFF{30.0f};

    /**
     * @brief Low-pass cutoff frequency in Hz (voice-AM bandwidth guard).
     *
     * Preserves the full AM voice bandwidth that receivers typically pass
     * (~4.5 kHz); at the 48 kHz input rate used by piam this also sits
     * comfortably below Nyquist.
     */
    static constexpr float LPF_CUTOFF{4'500.0f};

    /**
     * @brief AM modulation depth (m), canonical voice-AM value.
     *
     * Envelope = 0.5 * (1 + m * a). For m = 0.9 and AGC-normalised a in
     * [-target, +target] = [-0.8, +0.8], the envelope stays in [0.14, 0.86] -
     * clear of both zero-crossing (over-modulation carrier phase flip) and
     * unity (pad-drive saturation).
     */
    static constexpr float MODULATION_DEPTH{0.9f};

    /**
     * @brief AGC configuration tuned for AM voice transmission.
     *
     * Target 0.8 mirrors the SSB tracker (headroom + audible loudness
     * trade-off). Attack / decay intentionally diverge from SSB: AM needs
     * broadcast-style leveller timing so the AGC control envelope stays
     * well below the lowest voice fundamental (~85 Hz for male speech).
     * An AGC that tracks into the audio band modulates its own gain within
     * each glottal period, producing IM distortion that widens the DSB-FC
     * spectrum well beyond the LPF guard - on AM that spill goes straight
     * out on air as adjacent-channel splatter. At 48 kHz the 1-pole
     * coefficients translate to:
     *   attack = 0.003   -> tau ~= 6.9 ms  (control cutoff ~23 Hz, below
     *                                       the 30 Hz HPF and voice band)
     *   decay  = 0.0001  -> tau ~= 208 ms  (classic broadcast release,
     *                                       avoids inter-syllable pumping)
     * initialEnvelope is seeded at target so the first sample sees gain = 1.0
     * and stays within the envelope clamp - a small seed (as SSB uses) would
     * saturate into a startup pop. Convergence to steady-state voice level
     * is a ~500 ms gentle fade-in governed by the decay time constant.
     */
    static constexpr AgcConfig AM_AGC_CONFIG{
        .target          = 0.8f,
        .attack          = 0.003f,
        .decay           = 0.0001f,
        .initialEnvelope = 0.8f,
    };

    Biquad hpf_;              ///< DC block.
    Biquad lpf_;              ///< Voice-AM bandwidth guard.
    Agc agc_{AM_AGC_CONFIG};  ///< Scalar automatic gain control.
};
