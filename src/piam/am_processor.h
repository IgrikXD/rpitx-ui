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
     * Target 0.8 mirrors the SSB configuration (headroom + audible loudness
     * trade-off), attack/decay match the SSB voice tracker. Unlike SSB,
     * initialEnvelope is seeded at target so the first sample sees gain = 1.0
     * and the envelope stays within the unity clamp - a small seed (as used
     * by SSB) would produce an initial gain of target / seed on the first
     * real sample and saturate the AM envelope into a startup pop. The
     * remaining convergence to the true signal level is a gentle ramp-in
     * governed by the decay time constant (~21 ms at 48 kHz).
     */
    static constexpr AgcConfig AM_AGC_CONFIG{
        .target          = 0.8f,
        .attack          = 0.1f,
        .decay           = 0.001f,
        .initialEnvelope = 0.8f,
    };

    Biquad hpf_;              ///< DC block.
    Biquad lpf_;              ///< Voice-AM bandwidth guard.
    Agc agc_{AM_AGC_CONFIG};  ///< Scalar automatic gain control.
};
