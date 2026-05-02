/**
 * @file rds_modulator.h
 * @brief RDS biphase modulator producing the 57 kHz subcarrier baseband.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 25.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <array>

#include "rds_encoder.h"
#include "rds_pulse.h"

/**
 * @brief Streaming RDS baseband modulator.
 *
 * Wraps an RdsEncoder and produces the per-228-kHz-sample MPX-band RDS
 * signal: differential encoding -> RRC-shaped Manchester biphase pulses
 * (overlap-add of the precomputed RDS_PULSE_SAMPLES kernel) -> mixing onto
 * the 57 kHz subcarrier as a 4-phase walk [0, +s, 0, -s] (which is
 * sin(2 pi * 57 kHz * t) sampled at 228 kHz, i.e. exactly four samples per
 * subcarrier cycle, phase-locked to the 19 kHz pilot's rising zero crossing
 * to satisfy EN 50067).
 *
 * The modulator owns the encoder and pulls a fresh group whenever its
 * 104-bit buffer empties; callers therefore only ever interact with the
 * sample-rate output, not with bit timing or group cycling.
 *
 * @code
 * RdsModulator mod{};
 * mod.encoder().setPi(0xFFFF);
 * for (int i = 0; i < 228'000; ++i) {
 *     const float rdsSample{mod.nextSample()};   // one 228 kHz baseband sample
 *     ...
 * }
 * @endcode
 */
class RdsModulator {
public:
    RdsModulator() = default;

    RdsModulator(const RdsModulator&)            = delete;
    RdsModulator& operator=(const RdsModulator&) = delete;
    RdsModulator(RdsModulator&&)                 = delete;
    RdsModulator& operator=(RdsModulator&&)      = delete;

    /**
     * @brief Access the underlying encoder for parameter updates.
     *
     * Exposed so main() can wire CLI flags (PI / PS / RT / TA) directly to
     * the encoder without the modulator having to mirror every setter.
     *
     * @return Reference to the owned encoder.
     */
    [[nodiscard]] RdsEncoder& encoder() {
        return encoder_;
    }

    /**
     * @brief Generate one 228 kHz baseband sample of the RDS subcarrier.
     *
     * Internally advances the bit / pulse / subcarrier-phase state machine.
     * The output is normalised to [-1, +1] (worst-case 3-pulse overlap-add
     * peak divided out via rdsBiphasePulseOverlapPeak()), so callers can
     * scale by an absolute deviation budget and read the result as a literal
     * peak deviation contribution in the same units as that budget.
     *
     * @return RDS baseband sample at 228 kHz, in [-1, +1].
     */
    [[nodiscard]] float nextSample();

private:
    /**
     * @brief Pull the next bit from the buffer, refilling from the encoder
     *        when exhausted.
     * @return Next raw RDS bit (0 or 1, before differential encoding).
     */
    int nextBit();

    /**
     * @brief Pre-fill the overlap-add buffer so the first emitted sample
     *        already sees the steady-state 3-pulse overlap.
     *
     * Without priming, the cold-start modulator would emit one bit period
     * of single-pulse output followed by one of two-pulse overlap before
     * reaching the proper three-pulse overlap-add state - an audible RRC
     * edge transient on the subcarrier. We stamp the first two pulses
     * up-front and place the read head where the third pulse (stamped by
     * the first nextSample() call) will complete the 3-pulse overlap.
     *
     * Called lazily from nextSample() rather than from the constructor so
     * priming consumes bits from the encoder configured by main()
     * (PI / PS / RT / TA), not the default-constructed encoder state.
     */
    void prime();

    /**
     * @brief Apply differential encoding to a raw RDS bit.
     *
     * EN 50067 3.2.1.6: differential encoding XORs the current bit with
     * the previous output, which keeps the receiver agnostic to absolute
     * carrier polarity (a 180-deg phase ambiguity is inherent to BPSK).
     *
     * @param rawBit Raw RDS bit from the encoder (0 or 1).
     * @return Differentially-encoded bit (0 or 1).
     */
    int differentialEncode(int rawBit) {
        lastEncoded_ ^= rawBit & 1;
        return lastEncoded_;
    }

    /**
     * @brief Stamp a new biphase pulse into the rolling overlap-add buffer.
     *
     * The RDS pulse spans three RDS bit periods (RDS_PULSE_SAMPLES samples
     * at 228 kHz), so successive pulses overlap by two bits. The pulse is
     * either added or subtracted depending on the differentially-encoded
     * bit polarity, which realises the Manchester biphase keying.
     *
     * @param invert If true, subtract the pulse (negative polarity); else add.
     */
    void stampPulse(bool invert);

    /**
     * @brief Group-bit buffer; refilled from the encoder whenever empty.
     */
    std::array<int, RDS_BITS_PER_GROUP> bitBuffer_{};

    /**
     * @brief Read position in bitBuffer_; equals RDS_BITS_PER_GROUP when empty.
     */
    int bitPos_{RDS_BITS_PER_GROUP};

    /**
     * @brief Differential-encoder state (last emitted bit).
     */
    int lastEncoded_{0};

    /**
     * @brief Rolling overlap-add buffer of partially-emitted RDS pulses.
     *
     * Sized to RDS_PULSE_SAMPLES so a freshly stamped pulse never wraps
     * onto itself before being read out. Indexed circularly via in_/out_
     * position counters, which advance by 1 sample per nextSample() and
     * by RDS_SAMPLES_PER_BIT per stampPulse().
     */
    std::array<float, RDS_PULSE_SAMPLES> overlapBuffer_{};

    /**
     * @brief Write head into overlapBuffer_, advanced by RDS_SAMPLES_PER_BIT
     *        each time a new pulse is stamped.
     */
    int writeIndex_{0};

    /**
     * @brief Read head into overlapBuffer_, advanced by 1 each nextSample().
     *
     * Set by prime() on the first nextSample() call to 2 * RDS_SAMPLES_PER_BIT
     * - 1, the slot that holds the full 3-pulse overlap once that same call
     * stamps the third pulse.
     */
    int readIndex_{0};

    /**
     * @brief Sample countdown to the next bit boundary; the first nextSample()
     *        call (after priming) fetches a bit and stamps the third pulse.
     */
    int samplesToNextBit_{static_cast<int>(RDS_SAMPLES_PER_BIT)};

    /**
     * @brief Gates the one-shot prime() call from nextSample().
     */
    bool primed_{false};

    /**
     * @brief 57 kHz subcarrier phase index in [0, 4).
     *
     * 228 kHz / 57 kHz = 4 exactly, so the trigonometric carrier collapses
     * to a 4-element table. We use the sine sequence [0, +1, 0, -1] (as
     * opposed to the cosine [+1, 0, -1, 0]) so the subcarrier crosses zero
     * with positive slope at sample 0, in phase with the 19 kHz pilot. That
     * also matches Christophe Jacquet's reference modulator, so receivers
     * built against PiFmRds lock without a sign flip.
     */
    int subcarrierPhase_{0};

    RdsEncoder encoder_;  ///< Owned bit-stream source.
};
