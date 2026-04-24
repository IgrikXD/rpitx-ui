/**
 * @file ssb_processor.h
 * @brief Single-sideband (SSB) modulation processor.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstdint>

#include "agc.h"
#include "biquad.h"
#include "hilbert.h"
#include "iq_sample.h"

/**
 * @brief SSB sideband selection mode.
 */
enum class SsbMode : uint8_t {
    USB,  ///< Upper sideband.
    LSB   ///< Lower sideband.
};

/**
 * @brief Streaming SSB modulator with bandpass filtering and AGC.
 *
 * Processes normalized float audio into analytic IQ output
 * suitable for RF transmission via sendiq.
 *
 * DSP chain: HPF 300 Hz -> LPF 3000 Hz -> Hilbert transform -> AGC.
 *
 * @code
 * SsbProcessor ssb{SsbMode::USB};
 * auto iq{ssb.process(sample)};
 * @endcode
 */
class SsbProcessor {
public:
    /**
     * @brief Construct an SSB processor for the given sideband mode.
     * @param mode Sideband selection (USB or LSB).
     */
    explicit SsbProcessor(SsbMode mode);

    // Non-copyable, non-movable: the processor owns IIR state (Biquad delay
    // lines, Hilbert transformer taps, and Agc envelope estimate) that is
    // meaningful only in place - duplicating it mid-stream would fork the
    // DSP history, and moving it after construction is not a pattern any
    // current caller needs.
    SsbProcessor(const SsbProcessor&)            = delete;
    SsbProcessor& operator=(const SsbProcessor&) = delete;
    SsbProcessor(SsbProcessor&&)                 = delete;
    SsbProcessor& operator=(SsbProcessor&&)      = delete;

    /**
     * @brief Process a single normalized audio sample.
     * @param sample Input sample in [-1.0, 1.0] range.
     * @return IQ sample pair with AGC applied.
     */
    [[nodiscard]] IqSample process(float sample);

private:
    /**
     * @brief Output sample rate in Hz.
     */
    static constexpr float SAMPLE_RATE{48'000.0f};

    /**
     * @brief High-pass cutoff frequency in Hz.
     */
    static constexpr float HPF_CUTOFF{300.0f};

    /**
     * @brief Low-pass cutoff frequency in Hz.
     */
    static constexpr float LPF_CUTOFF{3000.0f};
    /**
     * @brief AGC configuration tuned for SSB voice transmission.
     */
    static constexpr AgcConfig SSB_AGC_CONFIG{
        .target          = 0.8f,
        .attack          = 0.1f,
        .decay           = 0.001f,
        .initialEnvelope = 1e-4f,
    };
    Biquad hpf_{
        Biquad::highPass(HPF_CUTOFF, SAMPLE_RATE)};  ///< High-pass filter (removes DC and sub-voice frequencies).
    Biquad lpf_{Biquad::lowPass(LPF_CUTOFF, SAMPLE_RATE)};  ///< Low-pass filter (limits voice bandwidth).
    Hilbert hilbert_;                                       ///< Hilbert transformer for analytic signal generation.
    Agc agc_{SSB_AGC_CONFIG};                               ///< Automatic gain control.
    SsbMode mode_;                                          ///< Sideband selection mode.
};
