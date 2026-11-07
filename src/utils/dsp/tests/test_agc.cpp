/**
 * @file test_agc.cpp
 * @brief Unit tests for the envelope-tracking AGC.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 05.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "agc.h"

namespace {
    /**
     * @brief Gain limit that never binds - keeps the gain = target / envelope arithmetic of the
     *        basic tests free of the limit.
     */
    constexpr float kNoGainLimit{std::numeric_limits<float>::infinity()};

    /**
     * @brief Gate at zero magnitude - every input decays the envelope, nothing holds it.
     */
    constexpr float kNoGate{0.0F};

    /**
     * @brief Snaps the envelope to |input| in one step (attack=decay=1) - simplifies analytical
     *        expectations across most basic tests by removing transient settling behavior.
     */
    constexpr AgcConfig kFastConfig{
        .target          = 1.0F,
        .attack          = 1.0F,
        .decay           = 1.0F,
        .initialEnvelope = 1e-4F,
        .maxGain         = kNoGainLimit,
        .gate            = kNoGate,
    };

    /**
     * @brief Peak-hold tracker (attack >> decay): envelope rises quickly under saturation but
     *        relaxes slowly when the magnitude collapses.
     */
    constexpr AgcConfig kAsymmetricConfig{
        .target          = 0.8F,
        .attack          = 0.5F,
        .decay           = 0.01F,
        .initialEnvelope = 1e-4F,
        .maxGain         = kNoGainLimit,
        .gate            = kNoGate,
    };

    /**
     * @brief Initial envelope below Agc::ENVELOPE_FLOOR - exercises the construct-time path where
     *        the AGC clamps gain to unity before any signal is observed.
     */
    constexpr AgcConfig kZeroEnvelopeConfig{
        .target          = 1.0F,
        .attack          = 1.0F,
        .decay           = 1.0F,
        .initialEnvelope = 0.0F,
        .maxGain         = kNoGainLimit,
        .gate            = kNoGate,
    };

    /**
     * @brief Non-unity target verifies that process() reads the configured target instead of a
     *        hardcoded 1.0.
     */
    constexpr AgcConfig kHalfTargetConfig{
        .target          = 0.5F,
        .attack          = 1.0F,
        .decay           = 1.0F,
        .initialEnvelope = 1e-4F,
        .maxGain         = kNoGainLimit,
        .gate            = kNoGate,
    };

    /**
     * @brief Snapping tracker whose gain may not exceed 4 - distinguishes the limit from the
     *        target / envelope arithmetic, which asks for 10 at |input| = 0.1.
     */
    constexpr AgcConfig kLimitedGainConfig{
        .target          = 1.0F,
        .attack          = 1.0F,
        .decay           = 1.0F,
        .initialEnvelope = 1e-4F,
        .maxGain         = 4.0F,
        .gate            = kNoGate,
    };

    /**
     * @brief Peak-hold tracker with a gate at 0.01: magnitudes below it leave the envelope where
     *        it is, so a pause does not raise the gain.
     */
    constexpr AgcConfig kGatedConfig{
        .target          = 0.8F,
        .attack          = 0.5F,
        .decay           = 0.01F,
        .initialEnvelope = 1e-4F,
        .maxGain         = kNoGainLimit,
        .gate            = 0.01F,
    };
}  // namespace

/**
 * @brief Floor branch returns unity gain when envelope and magnitude are both zero.
 *
 * Constructed with a zero initial envelope and fed a zero magnitude, the AGC must return
 * unity rather than diverge - the Agc::ENVELOPE_FLOOR clamp keeps the gain finite when no
 * signal is present.
 */
TEST(AgcTest, FloorYieldsUnityGainWhenEnvelopeNearZero) {
    Agc agc{kZeroEnvelopeConfig};
    const float gain{agc.updateGain(0.0F)};

    EXPECT_FLOAT_EQ(gain, 1.0F);
}

/**
 * @brief IQ process() scales each component by gain = target / |input|.
 *
 * With kFastConfig (attack=1) the envelope snaps to |input| immediately. For input magnitude
 * 0.5 against target=1 the gain resolves to 2.0 and both components are doubled - using a
 * non-unity gain distinguishes a correct scale-by-gain implementation from one that returns
 * the input unchanged.
 */
TEST(AgcTest, IqProcessAppliesGainOfTargetOverEnvelope) {
    Agc agc{kFastConfig};
    const IqSample input{.i = 0.3F, .q = 0.4F};  // |input| = 0.5
    const IqSample out{agc.process(input)};

    EXPECT_NEAR(out.i, 0.6F, 1e-5F);
    EXPECT_NEAR(out.q, 0.8F, 1e-5F);
}

/**
 * @brief Scalar process() normalizes its output to the configured target amplitude.
 *
 * With kHalfTargetConfig (target=0.5, attack=1) the envelope snaps to |input|=0.25, the gain
 * resolves to 2.0, and the scaled output equals target=0.5.
 */
TEST(AgcTest, ScalarProcessNormalizesToTargetAmplitude) {
    Agc agc{kHalfTargetConfig};
    const float out{agc.process(0.25F)};

    EXPECT_NEAR(out, 0.5F, 1e-5F);
}

/**
 * @brief A zero IqSample must produce a finite output, never NaN.
 *
 * When the envelope collapses to the floor as |input| goes to zero, the SUT clamps gain
 * instead of dividing by the near-zero envelope, so the output stays finite for both
 * components.
 */
TEST(AgcTest, ZeroInputDoesNotProduceNaN) {
    Agc agc{kFastConfig};
    const IqSample zeroInput{.i = 0.0F, .q = 0.0F};
    const IqSample out{agc.process(zeroInput)};

    EXPECT_TRUE(std::isfinite(out.i));
    EXPECT_TRUE(std::isfinite(out.q));
}

/**
 * @brief Asymmetric attack/decay produces fast rise, slow fall in the gain response.
 *
 * With attack=0.5 the envelope rises quickly under saturation, but decay=0.01 leaves it nearly
 * held when the magnitude collapses. The warm-up loop saturates with unity samples so the
 * envelope converges geometrically (env_N = 1 - 0.5^N * (1 - env_0)) to within float precision
 * of 1.0 before the measurement.
 *
 * After settling, dropping the magnitude to 0.1 advances the envelope by only one decay step:
 * env_new = 1 + decay*(0.1 - 1) = 0.991, so the gain shifts by ~0.007 - far less than a
 * symmetric tracker that would jump toward target/0.1. The 0.02 tolerance leaves ~3x slack
 * against the analytical estimate while still catching large regressions.
 */
TEST(AgcTest, AsymmetricAttackDecayCausesFastRiseSlowFall) {
    Agc agc{kAsymmetricConfig};

    constexpr int kEnvelopeSettleSamples{50};
    for (int i{0}; i < kEnvelopeSettleSamples; ++i) {
        [[maybe_unused]] const float warmupGain{agc.updateGain(1.0F)};
    }
    const float gainAtFullScale{agc.updateGain(1.0F)};
    const float gainAfterDrop{agc.updateGain(0.1F)};

    EXPECT_NEAR(gainAfterDrop, gainAtFullScale, 0.02F);
}

/**
 * @brief The gain never exceeds maxGain, however small the envelope is.
 *
 * With kLimitedGainConfig the envelope snaps to |input| = 0.1 and target / envelope asks for a
 * gain of 10; the limit of 4 has to win. An implementation that ignores the limit returns 10,
 * one that applies it to the envelope instead of the gain returns a different value again.
 */
TEST(AgcTest, GainIsLimitedToMaxGain) {
    Agc agc{kLimitedGainConfig};
    const float gain{agc.updateGain(0.1F)};

    EXPECT_FLOAT_EQ(gain, 4.0F);
}

/**
 * @brief While the magnitude stays below the gate the envelope holds and the gain does not rise.
 *
 * The tracker settles at full scale (gain 0.8), then sees 50 samples of 0.001 - a pause below
 * the gate of 0.01. Without the gate the envelope would decay by 1 - 0.99^50 = 39 % and the
 * gain would rise to 1.3; with it the gain has to stay at exactly 0.8. The first sample above
 * the gate (0.05) advances the envelope by one decay step again, so the gain rises above 0.8.
 */
TEST(AgcTest, EnvelopeHoldsWhileMagnitudeIsBelowGate) {
    Agc agc{kGatedConfig};

    constexpr int kEnvelopeSettleSamples{50};
    for (int i{0}; i < kEnvelopeSettleSamples; ++i) {
        [[maybe_unused]] const float warmupGain{agc.updateGain(1.0F)};
    }
    const float gainAtFullScale{agc.updateGain(1.0F)};
    constexpr int kPauseSamples{50};
    float gainInPause{gainAtFullScale};
    for (int i{0}; i < kPauseSamples; ++i) {
        gainInPause = agc.updateGain(0.001F);
    }
    const float gainAfterPause{agc.updateGain(0.05F)};

    EXPECT_FLOAT_EQ(gainInPause, gainAtFullScale);
    EXPECT_GT(gainAfterPause, gainAtFullScale);
}

/**
 * @brief The gate holds only the decay: a magnitude above the envelope attacks even below the gate.
 *
 * kGatedConfig starts with an envelope of 1e-4 and a gate of 0.01. A magnitude of 0.005 lies
 * below the gate but above the envelope, so the attack step applies:
 * env = 1e-4 + 0.5 * (0.005 - 1e-4) = 0.00255 and gain = 0.8 / 0.00255 = 313.7. A gate that
 * froze the envelope for every magnitude below it would leave the gain at 8000.
 */
TEST(AgcTest, AttackAppliesBelowTheGate) {
    Agc agc{kGatedConfig};
    const float gain{agc.updateGain(0.005F)};

    EXPECT_NEAR(gain, 313.7F, 0.5F);
}

namespace {
    struct AgcConfigRejectionTestCase {
        std::string_view name;
        float maxGain;
        float gate;
    };

    /**
     * @brief Render a rejection case as its name plus the offending limit and gate for gtest
     *        listings and failure messages.
     */
    void PrintTo(const AgcConfigRejectionTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {maxGain=" << testCase.maxGain << ", gate=" << testCase.gate << "}";
    }

    /**
     * @brief Limits and gates the constructor must reject: a gain limit below unity would
     *        attenuate every signal (a missing field reads as 0 and would mute the transmitter),
     *        a negative gate can never hold.
     */
    std::vector<AgcConfigRejectionTestCase> makeAgcConfigRejectionTestCases() {
        return {
            AgcConfigRejectionTestCase{"MaxGainBelowUnity", 0.5F, kNoGate},
            AgcConfigRejectionTestCase{"MaxGainZero", 0.0F, kNoGate},
            AgcConfigRejectionTestCase{"NegativeGate", kNoGainLimit, -0.1F},
        };
    }
}  // namespace

class AgcConfigRejectionTest : public ::testing::TestWithParam<AgcConfigRejectionTestCase> {};

/**
 * @brief A gain limit below 1 or a negative gate raises std::invalid_argument.
 */
TEST_P(AgcConfigRejectionTest, ThrowsInvalidArgument) {
    const auto& testCase{GetParam()};
    const AgcConfig config{
        .target          = kFastConfig.target,
        .attack          = kFastConfig.attack,
        .decay           = kFastConfig.decay,
        .initialEnvelope = kFastConfig.initialEnvelope,
        .maxGain         = testCase.maxGain,
        .gate            = testCase.gate,
    };

    EXPECT_THROW(Agc{config}, std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(InvalidLimitOrGate, AgcConfigRejectionTest,
                         ::testing::ValuesIn(makeAgcConfigRejectionTestCases()),
                         [](const ::testing::TestParamInfo<AgcConfigRejectionTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct AgcUpdateGainTestCase {
        std::string_view name;
        float magnitude;
        float expectedGain;
    };

    /**
     * @brief Render an AgcUpdateGainTestCase as its name plus magnitude/expected gain
     *        for gtest listings and failure messages.
     */
    void PrintTo(const AgcUpdateGainTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {magnitude=" << testCase.magnitude << ", expectedGain=" << testCase.expectedGain
            << "}";
    }

    /**
     * @brief Magnitudes above Agc::ENVELOPE_FLOOR where gain = target / |magnitude| (target=1).
     */
    std::vector<AgcUpdateGainTestCase> makeAboveFloorTestCases() {
        return {
            AgcUpdateGainTestCase{"Micro", 1e-5F, 1.0e5F},
            AgcUpdateGainTestCase{"Milli", 1e-3F, 1.0e3F},
            AgcUpdateGainTestCase{"Tenth", 0.1F, 10.0F},
            AgcUpdateGainTestCase{"Unity", 1.0F, 1.0F},
            AgcUpdateGainTestCase{"FiveTimes", 5.0F, 0.2F},
            AgcUpdateGainTestCase{"Hundred", 100.0F, 0.01F},
        };
    }

    /**
     * @brief Magnitudes at or below Agc::ENVELOPE_FLOOR where the AGC clamps gain to unity.
     *        NearZero is expressed as ENVELOPE_FLOOR / 10 so the "one order of magnitude below
     *        floor" relationship survives a floor retune without manual edits to the literal.
     */
    std::vector<AgcUpdateGainTestCase> makeBelowFloorTestCases() {
        return {
            AgcUpdateGainTestCase{"ExactlyZero", 0.0F, 1.0F},
            AgcUpdateGainTestCase{"NearZero", Agc::ENVELOPE_FLOOR / 10.0F, 1.0F},
        };
    }
}  // namespace

class AgcUpdateGainAboveFloorTest : public ::testing::TestWithParam<AgcUpdateGainTestCase> {};

/**
 * @brief Above the envelope floor the gain matches target / |magnitude| within float precision.
 *
 * Uses a relative tolerance scaled by the expected value: at magnitude=1e-5 the expected gain
 * is 1e5, so a single-precision relative error of ~1e-7 already exceeds any sane absolute
 * tolerance. Scaling the bound to the expected value keeps the check meaningful across the
 * magnitude sweep.
 */
TEST_P(AgcUpdateGainAboveFloorTest, MatchesTargetOverEnvelope) {
    const auto& testCase{GetParam()};
    Agc agc{kFastConfig};
    const float gain{agc.updateGain(testCase.magnitude)};

    EXPECT_NEAR(gain, testCase.expectedGain, std::abs(testCase.expectedGain) * 1e-4F);
}

INSTANTIATE_TEST_SUITE_P(MagnitudeSweep, AgcUpdateGainAboveFloorTest, ::testing::ValuesIn(makeAboveFloorTestCases()),
                         [](const ::testing::TestParamInfo<AgcUpdateGainTestCase>& info) {
                             return std::string{info.param.name};
                         });

class AgcUpdateGainBelowFloorTest : public ::testing::TestWithParam<AgcUpdateGainTestCase> {};

/**
 * @brief At or below the envelope floor the SUT clamps gain to unity to avoid divide-by-zero blow-up.
 */
TEST_P(AgcUpdateGainBelowFloorTest, ClampsToUnity) {
    const auto& testCase{GetParam()};
    Agc agc{kFastConfig};
    const float gain{agc.updateGain(testCase.magnitude)};

    EXPECT_FLOAT_EQ(gain, testCase.expectedGain);
}

INSTANTIATE_TEST_SUITE_P(MagnitudeSweep, AgcUpdateGainBelowFloorTest, ::testing::ValuesIn(makeBelowFloorTestCases()),
                         [](const ::testing::TestParamInfo<AgcUpdateGainTestCase>& info) {
                             return std::string{info.param.name};
                         });
