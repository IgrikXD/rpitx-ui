/**
 * @file rds_modulator.cpp
 * @brief RDS biphase modulator implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 25.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "rds_modulator.h"

#include <cstddef>

RdsModulator::RdsModulator()
    : overlapBuffer_(RDS_PULSE_SAMPLES, 0.0f), readIndex_{static_cast<int>(RDS_PULSE_SAMPLES) - 1} {
}

RdsEncoder& RdsModulator::encoder() {
    return encoder_;
}

int RdsModulator::nextBit() {
    if (bitPos_ >= RDS_BITS_PER_GROUP) {
        encoder_.nextGroupBits(bitBuffer_);
        bitPos_ = 0;
    }
    return bitBuffer_[static_cast<std::size_t>(bitPos_++)];
}

int RdsModulator::differentialEncode(int rawBit) {
    lastEncoded_ ^= rawBit & 1;
    return lastEncoded_;
}

void RdsModulator::stampPulse(bool invert) {
    const auto pulse{rdsPulse()};
    int idx{writeIndex_};
    for (std::size_t k{0}; k < pulse.size(); ++k) {
        const float v{invert ? -pulse[k] : pulse[k]};
        overlapBuffer_[static_cast<std::size_t>(idx)] += v;
        if (++idx >= static_cast<int>(overlapBuffer_.size())) {
            idx = 0;
        }
    }
    writeIndex_ += static_cast<int>(RDS_SAMPLES_PER_BIT);
    if (writeIndex_ >= static_cast<int>(overlapBuffer_.size())) {
        writeIndex_ -= static_cast<int>(overlapBuffer_.size());
    }
}

float RdsModulator::nextSample() {
    if (samplesToNextBit_ >= static_cast<int>(RDS_SAMPLES_PER_BIT)) {
        // Fetch the next raw bit, differentially encode it, and overlap-add
        // a fresh biphase pulse into the rolling buffer with polarity set
        // by the encoded bit. The pulse is laid down at writeIndex_ which
        // is one bit-period ahead of the read head, so by the time the
        // read head catches up the pulse will be fully shaped against any
        // earlier overlapping pulses.
        const int raw{nextBit()};
        const int enc{differentialEncode(raw)};
        stampPulse(enc == 1);
        samplesToNextBit_ = 0;
    }

    // Read out one sample from the overlap-add buffer and immediately clear
    // the slot so the buffer can be reused circularly without an explicit
    // "have we read this slot yet" book-keeping bit. Reads strictly trail
    // writes by one bit period (set up by readIndex_'s primer in the ctor).
    float sample{overlapBuffer_[static_cast<std::size_t>(readIndex_)]};
    overlapBuffer_[static_cast<std::size_t>(readIndex_)] = 0.0f;
    if (++readIndex_ >= static_cast<int>(overlapBuffer_.size())) {
        readIndex_ = 0;
    }

    // Modulate onto the 57 kHz subcarrier as a 4-phase walk. With Fs = 228
    // kHz and Fc = 57 kHz, sin(2 pi Fc / Fs * n) = sin(pi/2 * n) walks
    // through {0, +1, 0, -1} - exactly the [0, +s, 0, -s] sequence used by
    // Christophe Jacquet's reference RDS encoder. Sine form (rather than
    // cosine) is the canonical EN 50067 phase: starts at zero with a rising
    // slope, so the subcarrier is phase-locked to the 19 kHz pilot's rising
    // zero crossing (which is itself sine-form here).
    switch (subcarrierPhase_) {
        case 0:
        case 2:
            sample = 0.0f;
            break;
        case 1:
            // Pass-through: sample = sample.
            break;
        case 3:
            sample = -sample;
            break;
    }
    subcarrierPhase_ = (subcarrierPhase_ + 1) % 4;
    ++samplesToNextBit_;

    return sample;
}
