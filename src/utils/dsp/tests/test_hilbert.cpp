/**
 * @file test_hilbert.cpp
 * @brief Unit tests for the Blackman-windowed Hilbert FIR transformer.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 05.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include "hilbert.h"

/**
 * @brief Default-constructed Hilbert reports DEFAULT_TAPS via taps().
 *
 * Pinning the no-arg ctor to the public constant prevents accidental drift
 * between the documented default and the in-code default-argument value.
 */
TEST(HilbertTest, DefaultTapCountMatchesPublicConstant) {
    Hilbert hilbert{};

    EXPECT_EQ(hilbert.taps(), Hilbert::DEFAULT_TAPS);
}

/**
 * @brief Minimum valid tap count (3) constructs without throwing.
 *
 * Three is the lower bound of the valid domain (taps >= 3 and odd). Together
 * with HilbertConstructorRejectionTest this pins the validation boundary on
 * both sides.
 */
TEST(HilbertTest, AcceptsMinimumValidTapCount) {
    EXPECT_NO_THROW(Hilbert{3});
}

/**
 * @brief Impulse fed into Hilbert appears on the I channel exactly delay() samples later.
 *
 * I is a pure delay line matched to the FIR group delay on Q. Using taps=31
 * (delay=15) keeps the test independent of DEFAULT_TAPS while remaining short
 * enough to step through one sample at a time. Q-channel correctness is
 * covered separately by QChannelImpulseResponseIsAntisymmetric (kernel
 * structure) and EnvelopeStaysNearUnityForSteadyStateSinusoid (steady-state
 * amplitude).
 */
TEST(HilbertTest, IChannelMatchesInputAfterGroupDelay) {
    constexpr int kTaps{31};
    Hilbert hilbert{kTaps};

    const IqSample firstSample{hilbert.process(1.0F)};
    EXPECT_FLOAT_EQ(firstSample.i, 0.0F);

    for (int n{1}; n < hilbert.delay(); ++n) {
        const IqSample intermediateSample{hilbert.process(0.0F)};
        EXPECT_FLOAT_EQ(intermediateSample.i, 0.0F);
    }
    const IqSample atDelaySample{hilbert.process(0.0F)};

    EXPECT_FLOAT_EQ(atDelaySample.i, 1.0F);
}

/**
 * @brief Q-channel impulse response is antisymmetric around the center tap.
 *
 * The Blackman-windowed Hilbert FIR uses an antisymmetric kernel
 * (coeffs[i] = -coeffs[taps-1-i]); feeding an impulse and reading kTaps
 * samples reproduces the kernel in time order on the Q channel, so
 * antisymmetry holds pair-wise and the center sample is exactly zero. The
 * non-trivial-kernel check guards against a degenerate Q always returning 0
 * (which would satisfy antisymmetry vacuously). The positive-sign check at
 * k=+1 guards against a global sign flip antisymmetry is invariant to - the
 * Hilbert kernel 2/(pi*k) gives positive coefficients at k>0.
 *
 * The 1e-6 antisymmetry tolerance leaves ~10x margin against the ~1e-7
 * float roundoff expected for an antisymmetric coefficient pair derived
 * from the same windowed-kernel formula.
 */
TEST(HilbertTest, QChannelImpulseResponseIsAntisymmetric) {
    constexpr int kTaps{31};
    constexpr float kAntisymmetryTolerance{1e-6F};
    Hilbert hilbert{kTaps};

    std::vector<float> qResponse;
    qResponse.reserve(kTaps);

    const IqSample firstSample{hilbert.process(1.0F)};
    qResponse.push_back(firstSample.q);
    for (int n{1}; n < kTaps; ++n) {
        const IqSample sample{hilbert.process(0.0F)};
        qResponse.push_back(sample.q);
    }

    for (int i{0}; i < kTaps / 2; ++i) {
        EXPECT_NEAR(qResponse[i], -qResponse[kTaps - 1 - i], kAntisymmetryTolerance);
    }
    EXPECT_FLOAT_EQ(qResponse[kTaps / 2], 0.0F);
    EXPECT_GT(qResponse[kTaps / 2 + 1], 0.0F);

    EXPECT_TRUE(std::any_of(qResponse.cbegin(), qResponse.cend(), [](float qSample) { return qSample != 0.0F; }));
}

/**
 * @brief A steady-state real sinusoid produces a unit-amplitude analytic envelope.
 *
 * For a real sin input the analytic Q approximates -cos(omega * n) within
 * the FIR's passband, so |i + jq| = sqrt(sin^2 + cos^2) = 1 for a
 * unit-amplitude tone. We warm the filter for kWarmupSamples = kTaps - the
 * FIR has no IIR memory, so the buffer is fully primed after kTaps samples
 * and subsequent output is in steady state.
 *
 * The 5e-4 tolerance was empirically calibrated against this 1 kHz / 48 kHz
 * / 255-tap Blackman configuration: actual ripple peaks at ~1.45e-4 (max
 * envelope rounds bit-exact to 1.0F; min sits near 0.99986). 5e-4 leaves
 * ~3.5x slack against the measurement.
 */
TEST(HilbertTest, EnvelopeStaysNearUnityForSteadyStateSinusoid) {
    constexpr int kTaps{255};
    constexpr float kSampleRate{48'000.0F};
    constexpr float kToneFrequency{1'000.0F};
    constexpr float kOmega{2.0F * std::numbers::pi_v<float> * kToneFrequency / kSampleRate};
    constexpr int kWarmupSamples{kTaps};
    constexpr int kMeasureSamples{kTaps};
    constexpr float kEnvelopeTolerance{5e-4F};

    Hilbert hilbert{kTaps};

    for (int n{0}; n < kWarmupSamples; ++n) {
        [[maybe_unused]] const IqSample warmupSample{hilbert.process(std::sin(kOmega * static_cast<float>(n)))};
    }

    std::vector<float> magnitudes;
    magnitudes.reserve(kMeasureSamples);
    for (int n{kWarmupSamples}; n < kWarmupSamples + kMeasureSamples; ++n) {
        const IqSample sample{hilbert.process(std::sin(kOmega * static_cast<float>(n)))};
        magnitudes.push_back(std::hypot(sample.i, sample.q));
    }

    const auto [minMagnitude, maxMagnitude]{std::ranges::minmax(magnitudes)};

    EXPECT_NEAR(maxMagnitude, 1.0F, kEnvelopeTolerance);
    EXPECT_NEAR(minMagnitude, 1.0F, kEnvelopeTolerance);
}

/**
 * @brief Zero input produces zero output on both channels for any number of samples.
 *
 * A linear FIR fed zeros holds zero output. We run for kZeroInputSamples =
 * 3 * kTaps so the delay line cycles fully three times; accumulator drift
 * or stale state on either channel would surface as a non-zero sample
 * somewhere in the run.
 */
TEST(HilbertTest, ZeroInputProducesZeroOutput) {
    constexpr int kTaps{31};
    constexpr int kZeroInputSamples{kTaps * 3};

    Hilbert hilbert{kTaps};

    for (int n{0}; n < kZeroInputSamples; ++n) {
        const IqSample sample{hilbert.process(0.0F)};
        EXPECT_FLOAT_EQ(sample.i, 0.0F);
        EXPECT_FLOAT_EQ(sample.q, 0.0F);
    }
}

namespace {
    /**
     * @brief Tap counts the constructor must reject - sub-minimum (0, 1, 2) and even (4, 256).
     */
    std::vector<int> makeRejectedTapCounts() {
        return {0, 1, 2, 4, 256};
    }

    /**
     * @brief Valid odd tap counts spanning the boundary (3), small sizes, and production values.
     */
    std::vector<int> makeGeometryTapCounts() {
        return {3, 5, 31, 127, 255, 511};
    }
}  // namespace

class HilbertConstructorRejectionTest : public ::testing::TestWithParam<int> {};

/**
 * @brief Constructor throws std::invalid_argument for tap counts outside the valid domain.
 *
 * The valid domain is odd taps >= 3: counts below 3 leave too few samples
 * for a centered FIR, even counts break the antisymmetry of the impulse
 * response around the middle tap. Per-case parametrization isolates which
 * sub-domain failed.
 */
TEST_P(HilbertConstructorRejectionTest, ThrowsInvalidArgumentForUnsupportedSize) {
    const int taps{GetParam()};

    EXPECT_THROW(Hilbert{taps}, std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(OutOfDomain, HilbertConstructorRejectionTest, ::testing::ValuesIn(makeRejectedTapCounts()),
                         [](const ::testing::TestParamInfo<int>& info) { return "Taps" + std::to_string(info.param); });

class HilbertGeometryTest : public ::testing::TestWithParam<int> {};

/**
 * @brief taps() round-trips the constructor argument and delay() equals (taps-1)/2.
 *
 * The FIR is symmetric around its center tap, so the group delay is the
 * half-width (taps - 1) / 2. The taps() check is a sanity assertion that
 * the constructor accepted and stored the input. Integer arithmetic, no
 * tolerance needed.
 */
TEST_P(HilbertGeometryTest, TapsAndDelayMatchInput) {
    const int taps{GetParam()};
    Hilbert hilbert{taps};

    EXPECT_EQ(hilbert.taps(), taps);
    EXPECT_EQ(hilbert.delay(), (taps - 1) / 2);
}

INSTANTIATE_TEST_SUITE_P(SizeSweep, HilbertGeometryTest, ::testing::ValuesIn(makeGeometryTapCounts()),
                         [](const ::testing::TestParamInfo<int>& info) { return "Taps" + std::to_string(info.param); });
