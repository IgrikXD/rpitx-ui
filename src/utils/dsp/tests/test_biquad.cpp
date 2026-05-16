/**
 * @file test_biquad.cpp
 * @brief Unit tests for the Biquad IIR filter and its design factories.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 05.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "biquad.h"

namespace {

    /**
     * @brief Drive the biquad with a unit-amplitude sinusoid at the requested frequency
     *        and return the steady-state output peak amplitude.
     *
     * After kSettleSamples warmup samples the IIR transient has decayed below the noise
     * floor of the assertions below for any cutoff/rate combination exercised here; the
     * peak of the next kMeasureSamples then reads |H(omega)| of the filter. Used by every
     * frequency-response test in this TU.
     */
    float steadyStatePeak(Biquad& biquad, float freqHz, float sampleRate) {
        constexpr int kSettleSamples{4000};
        constexpr int kMeasureSamples{2000};
        const float omega{2.0F * std::numbers::pi_v<float> * freqHz / sampleRate};

        for (int n{0}; n < kSettleSamples; ++n) {
            [[maybe_unused]] const float warmupOutput{biquad.process(std::sin(omega * static_cast<float>(n)))};
        }

        float peak{0.0F};
        for (int n{0}; n < kMeasureSamples; ++n) {
            const float sampleIndex{static_cast<float>(kSettleSamples + n)};
            const float output{biquad.process(std::sin(omega * sampleIndex))};
            peak = std::max(peak, std::abs(output));
        }
        return peak;
    }

}  // namespace

/**
 * @brief BUTTERWORTH_Q exposes 1/sqrt(2) - the canonical 2nd-order Butterworth pole-pair Q.
 *
 * Pinning the public constant against the formula prevents drift between the documented
 * value (used at call sites for custom-Q cascade design) and the in-class initializer.
 * Both sides resolve to the same `1.0F / std::numbers::sqrt2_v<float>` expression so
 * equality is bit-exact.
 */
TEST(BiquadTest, ButterworthQConstantMatchesOneOverSqrtTwo) {
    EXPECT_FLOAT_EQ(Biquad::BUTTERWORTH_Q, 1.0F / std::numbers::sqrt2_v<float>);
}

/**
 * @brief Sustained zero input produces zero output across a multi-sample run.
 *
 * Verifies that delay lines initialize to zero (no construction-time noise) and that the
 * Direct-Form-I difference equation preserves the all-zero state across iterations. The
 * highPass factory choice is incidental - the property holds for any biquad coefficients
 * given zero state and input. Running 100 samples (>> 2-sample delay line) ensures any
 * drift would surface as a non-zero output.
 */
TEST(BiquadTest, ZeroInputProducesZeroOutput) {
    constexpr float kCutoffHz{1'000.0F};
    constexpr float kSampleRate{48'000.0F};
    constexpr int kZeroInputSamples{100};

    Biquad biquad{Biquad::highPass(kCutoffHz, kSampleRate)};

    for (int n{0}; n < kZeroInputSamples; ++n) {
        const float output{biquad.process(0.0F)};
        EXPECT_FLOAT_EQ(output, 0.0F);
    }
}

/**
 * @brief Process loop output matches the convolution of input with the captured impulse response.
 *
 * For any LTI filter y[n] = sum_k h[k] * x[n-k]. We capture h[0..5] from one fresh biquad
 * fed an impulse, then drive a second fresh biquad with a known mixed-sign input pattern
 * and verify each output equals the truncated convolution sum. Bugs that break LTI -
 * swapped y1/y2, flipped a1 sign, reversed delay-line update order - would surface here.
 * Filter type/cutoff is incidental; the test validates process(), not the coefficients.
 * For 6 multiplies of values bounded by ~1, expected absolute roundoff is on the order
 * of 1e-7 (float relative epsilon ~6e-8 per op), so the 1e-5 tolerance leaves ~100x
 * margin against the divergence between the recursive Direct-Form-I path (SUT) and the
 * open-loop convolution path (reference).
 */
TEST(BiquadTest, ProcessLoopMatchesConvolutionAgainstImpulseResponse) {
    constexpr float kCutoffHz{2'000.0F};
    constexpr float kSampleRate{48'000.0F};
    constexpr std::array<float, 6> kInputPattern{0.5F, 0.3F, -0.2F, 0.7F, -0.4F, 0.1F};
    constexpr float kConvolutionTolerance{1e-5F};

    Biquad biquadImpulse{Biquad::lowPass(kCutoffHz, kSampleRate)};
    std::array<float, kInputPattern.size()> impulseResponse{};
    impulseResponse[0] = biquadImpulse.process(1.0F);
    for (std::size_t n{1}; n < impulseResponse.size(); ++n) {
        impulseResponse[n] = biquadImpulse.process(0.0F);
    }

    Biquad biquadInput{Biquad::lowPass(kCutoffHz, kSampleRate)};
    std::array<float, kInputPattern.size()> output{};
    for (std::size_t n{0}; n < kInputPattern.size(); ++n) {
        output[n] = biquadInput.process(kInputPattern[n]);
    }

    for (std::size_t n{0}; n < kInputPattern.size(); ++n) {
        float reference{0.0F};
        for (std::size_t k{0}; k <= n; ++k) {
            reference += impulseResponse[n - k] * kInputPattern[k];
        }
        EXPECT_NEAR(output[n], reference, kConvolutionTolerance) << "Mismatch at sample " << n;
    }
}

/**
 * @brief A high-pass biquad fed sustained DC settles to zero output.
 *
 * The high-pass transfer has a zero at z=1 (DC), so |H(0)| = 0. After kSettleSamples the
 * IIR transient has decayed into the float noise floor; the 1e-3 tolerance leaves slack
 * against residual transient and Direct-Form-I roundoff at this cutoff/fs.
 */
TEST(BiquadTest, HighPassAttenuatesDc) {
    constexpr float kCutoffHz{300.0F};
    constexpr float kSampleRate{48'000.0F};
    constexpr int kSettleSamples{5000};
    constexpr float kDcResidualTolerance{1e-3F};

    Biquad biquad{Biquad::highPass(kCutoffHz, kSampleRate)};

    float lastOutput{0.0F};
    for (int i{0}; i < kSettleSamples; ++i) {
        lastOutput = biquad.process(1.0F);
    }

    EXPECT_NEAR(lastOutput, 0.0F, kDcResidualTolerance);
}

/**
 * @brief A high-pass biquad reaches near-unity peak amplitude well above its cutoff.
 *
 * At a tone frequency more than a decade above the 300 Hz Butterworth cutoff (4 kHz here)
 * the magnitude response is well into the passband, so |H(jw)| -> 1. The 0.05 tolerance
 * accommodates discrete-time prewarp drift.
 */
TEST(BiquadTest, HighPassPassesFrequencyWellAboveCutoff) {
    constexpr float kSampleRate{48'000.0F};
    constexpr float kCutoffHz{300.0F};
    constexpr float kPassbandToneHz{4'000.0F};
    constexpr float kPassbandTolerance{0.05F};

    Biquad biquad{Biquad::highPass(kCutoffHz, kSampleRate)};
    const float peak{steadyStatePeak(biquad, kPassbandToneHz, kSampleRate)};

    EXPECT_NEAR(peak, 1.0F, kPassbandTolerance);
}

/**
 * @brief A Butterworth-Q high-pass biquad reaches -3 dB amplitude at its cutoff frequency.
 *
 * Special case of the universal RBJ property |H(jw_c)| = Q with q = BUTTERWORTH_Q: the
 * magnitude at the corner equals 1/sqrt(2) ~= 0.7071, the defining -3 dB point of the
 * Butterworth response. The 0.05 tolerance accommodates bilinear prewarp shift of the
 * corner relative to the analog prototype.
 */
TEST(BiquadTest, HighPassMinusThreeDbAtCutoffWithButterworthQ) {
    constexpr float kSampleRate{48'000.0F};
    constexpr float kCutoffHz{1'000.0F};
    constexpr float kCutoffMagnitudeTolerance{0.05F};

    Biquad biquad{Biquad::highPass(kCutoffHz, kSampleRate)};
    const float peak{steadyStatePeak(biquad, kCutoffHz, kSampleRate)};

    EXPECT_NEAR(peak, Biquad::BUTTERWORTH_Q, kCutoffMagnitudeTolerance);
}

/**
 * @brief A non-default Q on the high-pass biquad sets the cutoff magnitude to Q.
 *
 * Mirror of the LPF Q-sweep at a single Q=2 case, validating that the q parameter is
 * wired through highPass() as well. For a 2nd-order RBJ section, |H(jw_c)| = Q
 * analytically, so Q=2 pins the magnitude at 2.0; if q were ignored and treated as the
 * Butterworth default (~0.707), the measured peak would land near 0.707 - far outside
 * the 5% relative tolerance around the expected 2.0.
 */
TEST(BiquadTest, HighPassCutoffMagnitudeMatchesCustomQ) {
    constexpr float kSampleRate{48'000.0F};
    constexpr float kCutoffHz{1'000.0F};
    constexpr float kCustomQ{2.0F};
    constexpr float kRelativeTolerance{0.05F};

    Biquad biquad{Biquad::highPass(kCutoffHz, kSampleRate, kCustomQ)};
    const float peak{steadyStatePeak(biquad, kCutoffHz, kSampleRate)};

    EXPECT_NEAR(peak, kCustomQ, kCustomQ * kRelativeTolerance);
}

/**
 * @brief A low-pass biquad fed sustained DC tracks the input unattenuated.
 *
 * The low-pass transfer has unity DC gain (|H(0)| = 1). After kSettleSamples the transient
 * has died out so the output equals the DC input within float roundoff. The 1e-3 tolerance
 * mirrors HighPassAttenuatesDc's "transient-floor" margin.
 */
TEST(BiquadTest, LowPassPassesDcCleanly) {
    constexpr float kCutoffHz{3'000.0F};
    constexpr float kSampleRate{48'000.0F};
    constexpr int kSettleSamples{5000};
    constexpr float kDcResidualTolerance{1e-3F};

    Biquad biquad{Biquad::lowPass(kCutoffHz, kSampleRate)};

    float lastOutput{0.0F};
    for (int i{0}; i < kSettleSamples; ++i) {
        lastOutput = biquad.process(1.0F);
    }

    EXPECT_NEAR(lastOutput, 1.0F, kDcResidualTolerance);
}

/**
 * @brief A low-pass biquad attenuates a tone well above cutoff to a small fraction of unity.
 *
 * The 2nd-order Butterworth analog prototype gives |H(jw)| = 1 / sqrt(1 + (w/w_c)^4) ~=
 * 0.007 at w/w_c=12 (1 kHz cutoff vs 12 kHz tone); the digital RBJ implementation rolls
 * off slightly steeper due to its zero at z=-1 (Nyquist) and measures around 0.004. The
 * 0.05 upper bound leaves wide slack against either while still flagging a regression to
 * first-order rolloff (-6 dB/oct gives |H| ~= 0.083, above the bound).
 */
TEST(BiquadTest, LowPassAttenuatesFrequencyWellAboveCutoff) {
    constexpr float kSampleRate{48'000.0F};
    constexpr float kCutoffHz{1'000.0F};
    constexpr float kStopbandToneHz{12'000.0F};
    constexpr float kStopbandUpperBound{0.05F};

    Biquad biquad{Biquad::lowPass(kCutoffHz, kSampleRate)};
    const float peak{steadyStatePeak(biquad, kStopbandToneHz, kSampleRate)};

    EXPECT_LT(peak, kStopbandUpperBound);
}

/**
 * @brief A Butterworth-Q low-pass biquad reaches -3 dB amplitude at its cutoff frequency.
 *
 * Symmetric counterpart of HighPassMinusThreeDbAtCutoffWithButterworthQ - same -3 dB
 * Butterworth shape at omega_c, same prewarp-drift tolerance.
 */
TEST(BiquadTest, LowPassMinusThreeDbAtCutoffWithButterworthQ) {
    constexpr float kSampleRate{48'000.0F};
    constexpr float kCutoffHz{2'000.0F};
    constexpr float kCutoffMagnitudeTolerance{0.05F};

    Biquad biquad{Biquad::lowPass(kCutoffHz, kSampleRate)};
    const float peak{steadyStatePeak(biquad, kCutoffHz, kSampleRate)};

    EXPECT_NEAR(peak, Biquad::BUTTERWORTH_Q, kCutoffMagnitudeTolerance);
}

/**
 * @brief Pre-emphasis exhibits unity gain well below the shelf onset frequency.
 *
 * H(s) = (1 + s*tau) / (1 + s*tau/boost) approaches unity as omega -> 0. The shelf
 * onset f_low = 1/(2*pi*tau) ~= 3.18 kHz for tau=50us; the 50 Hz test tone sits ~1.8
 * decades below f_low - deep in the unity plateau. The 0.05 tolerance covers prewarp
 * drift of the shelf onset.
 */
TEST(BiquadTest, PreEmphasisLowFrequencyPlateauIsUnity) {
    constexpr float kSampleRate{48'000.0F};
    constexpr float kTimeConstant{50e-6F};
    constexpr float kSubShelfToneHz{50.0F};
    constexpr float kPlateauTolerance{0.05F};

    Biquad biquad{Biquad::preEmphasis(kTimeConstant, kSampleRate)};
    const float peak{steadyStatePeak(biquad, kSubShelfToneHz, kSampleRate)};

    EXPECT_NEAR(peak, 1.0F, kPlateauTolerance);
}

/**
 * @brief Pre-emphasis at a high frequency rises towards the boost asymptote without crossing it.
 *
 * The analog prototype |H(jw)|^2 = (1 + (w*tau_z)^2) / (1 + (w*tau_p)^2) (tau_z=tau,
 * tau_p=tau/boost) reaches the boost asymptote only as omega -> infinity; at finite f the
 * magnitude sits below it. For tau=50us, fs=192 kHz, boost=16 the analog formula gives
 * ~12 at 60 kHz; bilinear-transform warping brings the digital implementation to ~14,
 * still under the 16 asymptote. Asserting peak > 0.75*boost AND peak < boost catches
 * regressions in either factor (zero or pole) of the shelf transfer.
 */
TEST(BiquadTest, PreEmphasisHighFrequencyShelfRisesTowardsBoost) {
    constexpr float kSampleRate{192'000.0F};
    constexpr float kTimeConstant{50e-6F};
    constexpr float kBoost{16.0F};
    constexpr float kHighFrequencyToneHz{60'000.0F};
    constexpr float kRiseLowerFraction{0.75F};

    Biquad biquad{Biquad::preEmphasis(kTimeConstant, kSampleRate, kBoost)};
    const float peak{steadyStatePeak(biquad, kHighFrequencyToneHz, kSampleRate)};

    EXPECT_GT(peak, kBoost * kRiseLowerFraction);
    EXPECT_LT(peak, kBoost);
}

/**
 * @brief A non-default boost scales the pre-emphasis shelf plateau proportionally.
 *
 * Mirror of PreEmphasisHighFrequencyShelfRisesTowardsBoost with boost=4 instead of the
 * default 16. Same 60 kHz tone, same 0.75*boost lower bound. If boost were ignored and
 * always treated as 16, the measured peak would land near 14 - far outside (3, 4) -
 * making the assertion fail. Pairs with the boost=16 single test to validate that the
 * boost parameter is wired through preEmphasis() into the resulting transfer function.
 */
TEST(BiquadTest, PreEmphasisCustomBoostScalesShelfPlateau) {
    constexpr float kSampleRate{192'000.0F};
    constexpr float kTimeConstant{50e-6F};
    constexpr float kCustomBoost{4.0F};
    constexpr float kHighFrequencyToneHz{60'000.0F};
    constexpr float kRiseLowerFraction{0.75F};

    Biquad biquad{Biquad::preEmphasis(kTimeConstant, kSampleRate, kCustomBoost)};
    const float peak{steadyStatePeak(biquad, kHighFrequencyToneHz, kSampleRate)};

    EXPECT_GT(peak, kCustomBoost * kRiseLowerFraction);
    EXPECT_LT(peak, kCustomBoost);
}

namespace {
    struct BiquadFrequencyTestCase {
        std::string_view name;
        float cutoffHz;
        float sampleRate;
    };

    /**
     * @brief Render a BiquadFrequencyTestCase as its name plus the (cutoff, fs) pair
     *        for gtest listings and failure messages.
     */
    void PrintTo(const BiquadFrequencyTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {cutoff=" << testCase.cutoffHz << "Hz, fs=" << testCase.sampleRate << "Hz}";
    }

    /**
     * @brief Cutoff/sample-rate pairs spanning telephony, broadcast, and high-rate audio
     *        for the high-pass DC convergence sweep. All cutoffs sit safely above 0 so the
     *        DC zero of the high-pass transfer remains analytically zero. The 48 kHz case
     *        uses a different cutoff than the HighPassAttenuatesDc single test to avoid
     *        re-validating the same operating point.
     */
    std::vector<BiquadFrequencyTestCase> makeHighPassDcTestCases() {
        return {
            BiquadFrequencyTestCase{"Cutoff50Fs8000", 50.0F, 8'000.0F},
            BiquadFrequencyTestCase{"Cutoff300Fs44100", 300.0F, 44'100.0F},
            BiquadFrequencyTestCase{"Cutoff100Fs48000", 100.0F, 48'000.0F},
            BiquadFrequencyTestCase{"Cutoff1000Fs96000", 1'000.0F, 96'000.0F},
            BiquadFrequencyTestCase{"Cutoff500Fs192000", 500.0F, 192'000.0F},
        };
    }

    /**
     * @brief Cutoff/sample-rate pairs for the low-pass DC pass-through sweep, with cutoffs
     *        well above DC so the unity-gain plateau analytically applies at f=0. The 48
     *        kHz case uses a different cutoff than the LowPassPassesDcCleanly single test
     *        to avoid re-validating the same operating point.
     */
    std::vector<BiquadFrequencyTestCase> makeLowPassDcTestCases() {
        return {
            BiquadFrequencyTestCase{"Cutoff1000Fs8000", 1'000.0F, 8'000.0F},
            BiquadFrequencyTestCase{"Cutoff3000Fs44100", 3'000.0F, 44'100.0F},
            BiquadFrequencyTestCase{"Cutoff500Fs48000", 500.0F, 48'000.0F},
            BiquadFrequencyTestCase{"Cutoff8000Fs96000", 8'000.0F, 96'000.0F},
            BiquadFrequencyTestCase{"Cutoff10000Fs192000", 10'000.0F, 192'000.0F},
        };
    }
}  // namespace

class BiquadHighPassDcResponseTest : public ::testing::TestWithParam<BiquadFrequencyTestCase> {};

/**
 * @brief Across a cutoff/rate sweep, the high-pass biquad's DC output settles to zero.
 *
 * 8000 samples comfortably exceeds the IIR transient duration even for the slowest case
 * (50 Hz cutoff at 8 kHz fs). The 1e-2 tolerance is loosened from the single-test 1e-3
 * because the lowest-cutoff cases have measurable residual at a fixed sample budget.
 */
TEST_P(BiquadHighPassDcResponseTest, OutputConvergesToZero) {
    constexpr int kSettleSamples{8000};
    constexpr float kDcResidualTolerance{1e-2F};
    const auto& testCase{GetParam()};

    Biquad biquad{Biquad::highPass(testCase.cutoffHz, testCase.sampleRate)};

    float lastOutput{0.0F};
    for (int i{0}; i < kSettleSamples; ++i) {
        lastOutput = biquad.process(1.0F);
    }

    EXPECT_NEAR(lastOutput, 0.0F, kDcResidualTolerance);
}

INSTANTIATE_TEST_SUITE_P(CornerRateMatrix, BiquadHighPassDcResponseTest, ::testing::ValuesIn(makeHighPassDcTestCases()),
                         [](const ::testing::TestParamInfo<BiquadFrequencyTestCase>& info) {
                             return std::string{info.param.name};
                         });

class BiquadLowPassDcResponseTest : public ::testing::TestWithParam<BiquadFrequencyTestCase> {};

/**
 * @brief Across a cutoff/rate sweep, the low-pass biquad's DC output settles to unity.
 *
 * Mirror of the high-pass DC sweep with expected value 1.0 - the LPF's unity-gain plateau
 * at DC. Same warmup count and tolerance as the high-pass sweep.
 */
TEST_P(BiquadLowPassDcResponseTest, OutputSettlesAtUnity) {
    constexpr int kSettleSamples{8000};
    constexpr float kDcResidualTolerance{1e-2F};
    const auto& testCase{GetParam()};

    Biquad biquad{Biquad::lowPass(testCase.cutoffHz, testCase.sampleRate)};

    float lastOutput{0.0F};
    for (int i{0}; i < kSettleSamples; ++i) {
        lastOutput = biquad.process(1.0F);
    }

    EXPECT_NEAR(lastOutput, 1.0F, kDcResidualTolerance);
}

INSTANTIATE_TEST_SUITE_P(CornerRateMatrix, BiquadLowPassDcResponseTest, ::testing::ValuesIn(makeLowPassDcTestCases()),
                         [](const ::testing::TestParamInfo<BiquadFrequencyTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct BiquadCustomQTestCase {
        std::string_view name;
        float q;
    };

    /**
     * @brief Render a BiquadCustomQTestCase as its name plus the Q value
     *        for gtest listings and failure messages.
     */
    void PrintTo(const BiquadCustomQTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {q=" << testCase.q << "}";
    }

    /**
     * @brief Q values spanning overdamped (0.5), neutral (1.0), and resonant (2.0, 4.0)
     *        regimes for the LPF cutoff-magnitude sweep. The Butterworth Q (~0.7071) is
     *        omitted to avoid duplicating LowPassMinusThreeDbAtCutoffWithButterworthQ.
     */
    std::vector<BiquadCustomQTestCase> makeCustomQTestCases() {
        return {
            BiquadCustomQTestCase{"Overdamped", 0.5F},
            BiquadCustomQTestCase{"Unity", 1.0F},
            BiquadCustomQTestCase{"MildPeak", 2.0F},
            BiquadCustomQTestCase{"SharpPeak", 4.0F},
        };
    }
}  // namespace

class BiquadLowPassResonanceTest : public ::testing::TestWithParam<BiquadCustomQTestCase> {};

/**
 * @brief Across a Q sweep, the low-pass biquad's cutoff magnitude tracks Q.
 *
 * For a 2nd-order RBJ section at omega = omega_c, |H(jw_c)| = Q analytically (Q sets
 * the height of the resonant peak). Ignoring q would collapse all cases to the
 * Butterworth default (~0.707), failing every assertion outside that region. At
 * fc=2000/fs=48000 (8.3% of Nyquist) the digital response stays within ~1% of analog
 * Q across the sweep, so the 5% relative tolerance leaves ~5x slack at worst (Q=4).
 */
TEST_P(BiquadLowPassResonanceTest, CutoffMagnitudeMatchesQ) {
    constexpr float kSampleRate{48'000.0F};
    constexpr float kCutoffHz{2'000.0F};
    constexpr float kRelativeTolerance{0.05F};
    const auto& testCase{GetParam()};

    Biquad biquad{Biquad::lowPass(kCutoffHz, kSampleRate, testCase.q)};
    const float peak{steadyStatePeak(biquad, kCutoffHz, kSampleRate)};

    EXPECT_NEAR(peak, testCase.q, testCase.q * kRelativeTolerance);
}

INSTANTIATE_TEST_SUITE_P(PoleQualitySweep, BiquadLowPassResonanceTest, ::testing::ValuesIn(makeCustomQTestCases()),
                         [](const ::testing::TestParamInfo<BiquadCustomQTestCase>& info) {
                             return std::string{info.param.name};
                         });
