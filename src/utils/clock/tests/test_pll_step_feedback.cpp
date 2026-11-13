/**
 * @file test_pll_step_feedback.cpp
 * @brief Unit tests for the error feedback over the frequency step of the carrier PLL.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 13.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "carrier_pll.h"
#include "fake_clock.h"
#include "pll_step_feedback.h"

namespace {
    /**
     * @brief Carrier on an integer step of the PLL multiplier of the stand-in clock, which every test leaves
     *        on BCM2711 with the pre-divider: 16 times half the crystal.
     */
    constexpr double INTEGER_STEP_CARRIER_HZ{432'000'000.0};

    /**
     * @brief Carrier whose PLL fraction lies far from an integer step (fraction 77672 of 1048576).
     */
    constexpr double MID_FRACTION_CARRIER_HZ{434'000'000.0};

    /**
     * @brief Highest deviation the feedback is constructed for - the white level of an SSTV picture.
     */
    constexpr float MAX_DEVIATION_HZ{2'300.0F};

    /**
     * @brief Samples of a test tone: one step of remainder moves the mean of 480 samples by 0.054 Hz.
     */
    constexpr std::size_t TONE_SAMPLES{480};

    /**
     * @brief Clock correction close to the largest one librpitx applies, in ppm: a unit of the fraction
     *        of the corrected clock is 1.000199 nominal steps.
     */
    constexpr double CLOCK_CORRECTION_PPM{-199.0};

    /**
     * @brief Deviation whose share of that correction, 119 Hz, exceeds the two steps the constructor adds (51.5 Hz).
     */
    constexpr float WIDE_DEVIATION_HZ{600'000.0F};

    /**
     * @brief Deviation the stand-in clock delivers for a requested one, in Hz above its unmodulated carrier.
     *
     * A request behind an integer step of the multiplier shows up as a deviation of about -27 MHz
     * (request above the step) or +27 MHz (request below the unmodulated carrier on the step).
     */
    [[nodiscard]] double deliveredDeviation(FakeClock& fakeClock, float requested) {
        const auto units{static_cast<double>(fakeClock.GetMasterFrac(requested)) -
                         static_cast<double>(fakeClock.GetMasterFrac(0.0))};
        return units * carrierStepHz(fakeClock);
    }

    /**
     * @brief Mean of the deviations the stand-in clock delivers for the requested ones.
     */
    [[nodiscard]] double meanDeliveredDeviation(FakeClock& fakeClock, std::span<const float> requests) {
        const double sum{
            std::accumulate(requests.begin(), requests.end(), 0.0, [&fakeClock](double total, float request) {
                return total + deliveredDeviation(fakeClock, request);
            })};
        return sum / static_cast<double>(requests.size());
    }
}  // namespace

namespace {
    struct ToneTestCase {
        std::string_view name;
        double carrier;
        float tone;
    };

    /**
     * @brief Render a ToneTestCase as its name, carrier and tone for gtest listings and failure messages.
     */
    void PrintTo(const ToneTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {carrier=" << testCase.carrier << ", tone=" << testCase.tone << "}";
    }

    /**
     * @brief Tones between two units of the fraction: the sync, black and white tones of an SSTV signal at a
     *        carrier far from an integer step (46.6, 58.3 and 89.3 units), a level on a carrier that sits on
     *        an integer step (73.8 units from fraction 0), and a tone smaller than one unit.
     */
    std::vector<ToneTestCase> makeToneTestCases() {
        return {
            ToneTestCase{"SyncTone", MID_FRACTION_CARRIER_HZ, 1'200.0F},
            ToneTestCase{"BlackLevel", MID_FRACTION_CARRIER_HZ, 1'500.0F},
            ToneTestCase{"WhiteLevel", MID_FRACTION_CARRIER_HZ, 2'300.0F},
            ToneTestCase{"LevelOnAnIntegerStepCarrier", INTEGER_STEP_CARRIER_HZ, 1'900.5F},
            ToneTestCase{"ToneBelowOneStep", MID_FRACTION_CARRIER_HZ, 10.0F},
        };
    }
}  // namespace

class PllStepFeedbackToneTest : public ::testing::TestWithParam<ToneTestCase> {};

/**
 * @brief A constant tone is delivered exactly on average although every sample lands on the 25.7 Hz grid.
 *
 * The remainder the feedback carries is smaller than one step, so the sum of the delivered
 * deviations differs from the sum of the requested ones by less than one step and their mean
 * over 480 samples by less than 0.054 Hz - without the feedback every sample would be rounded
 * down the same way, up to a whole step off. No single sample is a step or more away from the tone.
 */
TEST_P(PllStepFeedbackToneTest, DeliversTheToneOnAverage) {
    const auto& testCase{GetParam()};
    FakeClock fakeClock{.carrier = testCase.carrier};
    const double stepHz{carrierStepHz(fakeClock)};
    PllStepFeedback<FakeClock> pllStepFeedback{fakeClock, MAX_DEVIATION_HZ};
    std::vector<float> samples(TONE_SAMPLES, testCase.tone);

    pllStepFeedback.apply(samples);

    EXPECT_NEAR(meanDeliveredDeviation(fakeClock, samples),
                static_cast<double>(testCase.tone),
                stepHz / static_cast<double>(TONE_SAMPLES));
    for (const float sample: samples) {
        EXPECT_LT(std::abs(deliveredDeviation(fakeClock, sample) - static_cast<double>(testCase.tone)), stepHz);
    }
}

INSTANTIATE_TEST_SUITE_P(DeviationsAndCarriers, PllStepFeedbackToneTest, ::testing::ValuesIn(makeToneTestCases()),
                         [](const ::testing::TestParamInfo<ToneTestCase>& info) {
                             return std::string{info.param.name};
                         });

/**
 * @brief The remainder survives from one apply() call to the next.
 *
 * The transmitter feeds the feedback block by block, here one sample per call: with the remainder
 * kept, the mean of 480 samples of a 1200 Hz tone is within 0.054 Hz of the tone; a feedback that
 * started every call from zero would round each sample down to 46 units, 15.5 Hz below it.
 */
TEST(PllStepFeedbackTest, KeepsTheRemainderBetweenCalls) {
    constexpr float tone{1'200.0F};
    FakeClock fakeClock{.carrier = MID_FRACTION_CARRIER_HZ};
    const double stepHz{carrierStepHz(fakeClock)};
    PllStepFeedback<FakeClock> pllStepFeedback{fakeClock, MAX_DEVIATION_HZ};
    std::vector<float> samples(TONE_SAMPLES, tone);

    for (float& sample: samples) {
        pllStepFeedback.apply(std::span<float>{&sample, 1});
    }

    EXPECT_NEAR(meanDeliveredDeviation(fakeClock, samples),
                static_cast<double>(tone),
                stepHz / static_cast<double>(TONE_SAMPLES));
}

/**
 * @brief Idle samples keep the unmodulated carrier and the engine is never asked for less than it.
 *
 * The carrier sits 20 Hz above an integer step of the multiplier (fraction 0, 0.78 of a unit used
 * up), so a tone can leave a remainder of up to -20 Hz behind; 7, 8 and 9 samples of a 1500 Hz
 * tone end on different remainders. The zero samples that follow each tone must all be delivered
 * on the fraction of the unmodulated carrier and none may be requested below 0 Hz: the fraction
 * one unit lower belongs to the integer step below, 27 MHz away.
 */
TEST(PllStepFeedbackTest, KeepsIdleSamplesOnTheUnmodulatedCarrier) {
    constexpr double carrierAboveStep{20.0};
    constexpr float tone{1'500.0F};
    constexpr float idle{0.0F};
    constexpr std::size_t idleSamples{50};
    constexpr std::array<std::size_t, 3> toneRuns{7, 8, 9};
    FakeClock fakeClock{.carrier = INTEGER_STEP_CARRIER_HZ + carrierAboveStep};
    PllStepFeedback<FakeClock> pllStepFeedback{fakeClock, MAX_DEVIATION_HZ};
    std::vector<float> deviations;
    for (const std::size_t toneRun: toneRuns) {
        deviations.insert(deviations.end(), toneRun, tone);
        deviations.insert(deviations.end(), idleSamples, idle);
    }
    std::vector<float> requests{deviations};

    pllStepFeedback.apply(requests);

    for (std::size_t i{0}; i < requests.size(); ++i) {
        EXPECT_GE(requests[i], 0.0F) << "sample " << i;
        if (deviations[i] == idle) {
            EXPECT_EQ(fakeClock.GetMasterFrac(requests[i]), fakeClock.GetMasterFrac(0.0)) << "sample " << i;
        }
    }
}

/**
 * @brief A corrected clock is asked for more than the highest deviation, inside the validated range.
 *
 * The feedback counts in nominal steps while a unit of the fraction of the corrected clock is
 * 1.000199 of them, so a 600 kHz tone is requested 119 Hz - 4.6 steps - above itself: beyond the
 * two steps the constructor adds to the highest deviation, within them plus the 200 ppm it adds
 * as well.
 */
TEST(PllStepFeedbackTest, KeepsTheRequestsOfACorrectedClockInTheValidatedRange) {
    FakeClock fakeClock{.carrier = MID_FRACTION_CARRIER_HZ, .clockCorrectionPpm = CLOCK_CORRECTION_PPM};
    const double stepHz{carrierStepHz(fakeClock)};
    PllStepFeedback<FakeClock> pllStepFeedback{fakeClock, WIDE_DEVIATION_HZ};
    std::vector<float> samples(TONE_SAMPLES, WIDE_DEVIATION_HZ);

    pllStepFeedback.apply(samples);

    const double highestRequest{*std::ranges::max_element(samples)};
    EXPECT_GT(highestRequest, static_cast<double>(WIDE_DEVIATION_HZ) + 2.0 * stepHz);
    EXPECT_LT(highestRequest, static_cast<double>(WIDE_DEVIATION_HZ) * (1.0 + MAX_CLOCK_CORRECTION) + 2.0 * stepHz);
}

/**
 * @brief A carrier is refused when only the clock correction carries a request behind the integer step.
 *
 * The integer step lies four steps above the highest deviation: outside the two steps the
 * constructor adds to it, inside the 119 Hz the corrected clock is asked for on top of a 600 kHz tone.
 */
TEST(PllStepFeedbackTest, RefusesACarrierWhoseStepTheClockCorrectionReaches) {
    constexpr double stepsAboveDeviation{4.0};
    FakeClock fakeClock{.clockCorrectionPpm = CLOCK_CORRECTION_PPM};
    const double integerStep{INTEGER_STEP_CARRIER_HZ * (1.0 - CLOCK_CORRECTION_PPM * 1e-6)};
    fakeClock.carrier =
        integerStep - static_cast<double>(WIDE_DEVIATION_HZ) - stepsAboveDeviation * carrierStepHz(fakeClock);

    EXPECT_THROW((PllStepFeedback<FakeClock>{fakeClock, WIDE_DEVIATION_HZ}), std::runtime_error);
}

/**
 * @brief A deviation as wide as the distance between two integer steps is refused at any carrier.
 *
 * A request 27 MHz above the carrier has the fraction of the carrier again, so the fractions
 * show no wrap although an integer step lies in between.
 */
TEST(PllStepFeedbackTest, RefusesADeviationAsWideAsTheIntegerSteps) {
    constexpr float stepDistance{27'000'000.0F};
    FakeClock fakeClock{.carrier = MID_FRACTION_CARRIER_HZ};

    EXPECT_THROW((PllStepFeedback<FakeClock>{fakeClock, stepDistance}), std::runtime_error);
}

namespace {
    struct IntegerStepTestCase {
        std::string_view name;
        double stepAboveCarrier;  ///< Distance from the carrier up to the next integer step, in Hz.
        bool expectedAccepted;
    };

    /**
     * @brief Render an IntegerStepTestCase as its name, the distance to the integer step and the expected
     *        outcome for gtest listings and failure messages.
     */
    void PrintTo(const IntegerStepTestCase& testCase, std::ostream* os) {
        *os << std::boolalpha << testCase.name << " {stepAboveCarrier=" << testCase.stepAboveCarrier
            << ", expectedAccepted=" << testCase.expectedAccepted << "}";
    }

    /**
     * @brief Carriers at different distances below an integer step of the multiplier. The feedback
     *        requests up to one step and the clock correction (200 ppm, 0.5 Hz) above the highest
     *        deviation and keeps a second step in reserve, so the carrier is refused while the integer
     *        step lies within 2300 Hz + 52 Hz above it; a carrier on the integer step itself has the
     *        whole range above it free.
     */
    std::vector<IntegerStepTestCase> makeIntegerStepTestCases() {
        return {
            IntegerStepTestCase{"StepInsideTheDeviations", 1'000.0, false},
            IntegerStepTestCase{"StepRightAboveTheCarrier", 10.0, false},
            IntegerStepTestCase{"StepInsideTheReserve", 2'340.0, false},
            IntegerStepTestCase{"StepBehindTheReserve", 2'360.0, true},
            IntegerStepTestCase{"CarrierOnTheStep", 0.0, true},
        };
    }
}  // namespace

class PllStepFeedbackIntegerStepTest : public ::testing::TestWithParam<IntegerStepTestCase> {};

/**
 * @brief A carrier is refused exactly when a request could cross an integer step of the multiplier.
 *
 * The engine rewrites only the fraction, so a request behind the integer step would move the
 * carrier by a whole step (27 MHz with this clock). The diagnostic names the deviation and the
 * reason. An accepted carrier delivers the highest deviation - the one that comes closest to the
 * step - within one step of it on every sample.
 */
TEST_P(PllStepFeedbackIntegerStepTest, RefusesACarrierWhoseDeviationsCrossTheStep) {
    const auto& testCase{GetParam()};
    FakeClock fakeClock{.carrier = INTEGER_STEP_CARRIER_HZ - testCase.stepAboveCarrier};

    if (testCase.expectedAccepted == false) {
        try {
            PllStepFeedback<FakeClock>{fakeClock, MAX_DEVIATION_HZ};
            FAIL() << "the carrier was accepted";
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string{error.what()}.find(
                          "a frequency deviation of up to 2300 Hz crosses an integer step of the PLL multiplier"),
                      std::string::npos)
                << error.what();
        }
        return;
    }

    PllStepFeedback<FakeClock> pllStepFeedback{fakeClock, MAX_DEVIATION_HZ};
    std::vector<float> samples(TONE_SAMPLES, MAX_DEVIATION_HZ);
    pllStepFeedback.apply(samples);
    for (const float sample: samples) {
        EXPECT_LT(std::abs(deliveredDeviation(fakeClock, sample) - static_cast<double>(MAX_DEVIATION_HZ)),
                  carrierStepHz(fakeClock));
    }
}

INSTANTIATE_TEST_SUITE_P(CarrierDistances, PllStepFeedbackIntegerStepTest,
                         ::testing::ValuesIn(makeIntegerStepTestCases()),
                         [](const ::testing::TestParamInfo<IntegerStepTestCase>& info) {
                             return std::string{info.param.name};
                         });
