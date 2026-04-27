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

namespace {
    /**
     * @brief Size of the circular overlap-add buffer as a signed index bound.
     */
    constexpr int OVERLAP_BUFFER_SIZE{static_cast<int>(RDS_PULSE_SAMPLES)};

    /**
     * @brief 57 kHz sine subcarrier sampled at 228 kHz: [0, +1, 0, -1].
     */
    constexpr std::array<float, 4> SUBCARRIER_GAIN{0.0F, 1.0F, 0.0F, -1.0F};
}  // namespace

int RdsModulator::nextBit() {
    if (bitPos_ >= RDS_BITS_PER_GROUP) {
        encoder_.nextGroupBits(bitBuffer_);
        bitPos_ = 0;
    }

    return bitBuffer_[static_cast<std::size_t>(bitPos_++)];
}

void RdsModulator::stampPulse(bool invert) {
    const auto pulse{rdsPulse()};
    int idx{writeIndex_};

    for (std::size_t k{0}; k < pulse.size(); ++k) {
        const auto bufferIndex{static_cast<std::size_t>(idx)};
        if (invert) {
            overlapBuffer_[bufferIndex] -= pulse[k];
        } else {
            overlapBuffer_[bufferIndex] += pulse[k];
        }
        if (++idx >= OVERLAP_BUFFER_SIZE) {
            idx = 0;
        }
    }

    writeIndex_ += static_cast<int>(RDS_SAMPLES_PER_BIT);
    if (writeIndex_ >= OVERLAP_BUFFER_SIZE) {
        writeIndex_ -= OVERLAP_BUFFER_SIZE;
    }
}

float RdsModulator::nextSample() {
    if (samplesToNextBit_ >= static_cast<int>(RDS_SAMPLES_PER_BIT)) {
        // At each RDS bit boundary, fetch and differentially encode one bit,
        // then stamp its shaped pulse one bit period ahead of the read head.
        const int raw{nextBit()};
        const int enc{differentialEncode(raw)};

        stampPulse(enc == 1);
        samplesToNextBit_ = 0;
    }

    // Modulate onto the 57 kHz subcarrier as a 4-phase walk. With Fs = 228
    // kHz and Fc = 57 kHz, sin(2 pi Fc / Fs * n) = sin(pi/2 * n) walks
    // through {0, +1, 0, -1} - exactly the [0, +s, 0, -s] sequence used by
    // Christophe Jacquet's reference RDS encoder. Sine form (rather than
    // cosine) is the canonical EN 50067 phase: starts at zero with a rising
    // slope, so the subcarrier is phase-locked to the 19 kHz pilot's rising
    // zero crossing (which is itself sine-form here).
    const auto bufferIndex{static_cast<std::size_t>(readIndex_)};
    const float sample{overlapBuffer_[bufferIndex] * SUBCARRIER_GAIN[static_cast<std::size_t>(subcarrierPhase_)]};
    overlapBuffer_[bufferIndex] = 0.0F;

    // The read head stays one bit period behind the write head.
    if (++readIndex_ >= OVERLAP_BUFFER_SIZE) {
        readIndex_ = 0;
    }

    subcarrierPhase_ = (subcarrierPhase_ + 1) % static_cast<int>(SUBCARRIER_GAIN.size());
    ++samplesToNextBit_;

    return sample;
}
