/**
 * @file test_carrier_pll.cpp
 * @brief Unit tests for the carrier PLL divider choice, the PLL rate and the carrier step of its settings,
 *        and the integer steps of the PLL multiplier.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 10.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "carrier_pll.h"
#include "fake_clock.h"

namespace {
    struct PllDividerChoiceTestCase {
        std::string_view name;
        bool pi4;
        uint64_t frequency;
        int guardBand;
        int result;
        int divider;
        std::string_view expectedError;  ///< Part of the diagnostic; empty when the setting is accepted.
    };

    /**
     * @brief Render a PllDividerChoiceTestCase as its name and carrier for gtest listings and
     *        failure messages.
     */
    void PrintTo(const PllDividerChoiceTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {frequency=" << testCase.frequency << ", divider=" << testCase.divider << "}";
    }

    /**
     * @brief The carriers of the transmitters as librpitx resolves them on the two SoC families.
     *
     * A PLL rate below the minimum is refused for one of two causes. The dividers that keep the PLL
     * at its minimum rate start at 120 for a 2.5 MHz carrier on BCM2711, where two integer steps of
     * the multiplier lie 450 kHz of the PLL frequency apart: a guard band of +-225 kHz fills that
     * distance wherever the carrier lies, one of +-224.999 kHz only at some carriers. At 2.51046 MHz
     * they start at 120 as well, and +-226 kHz would fit only between the steps of divider 119, which
     * runs the PLL below its minimum. At 108 MHz on BCM2711 and at 96 MHz on BCM2835 every such
     * divider has an integer step on the carrier itself.
     */
    std::vector<PllDividerChoiceTestCase> makePllDividerChoiceTestCases() {
        return {
            PllDividerChoiceTestCase{"AcceptedWithThePredividerOnBcm2711", true, 434'000'000, 0, 0, 1, ""},
            PllDividerChoiceTestCase{"AcceptedWithADividerOnBcm2835", false, 434'000'000, 48'000, 0, 3, ""},
            PllDividerChoiceTestCase{"AcceptedAtTheMinimumRate", true, 100'000, 0, 0, 3000, ""},
            PllDividerChoiceTestCase{"RefusedAboveTheLimitOfAnyBoard",
                                     true,
                                     2'000'000'001,
                                     0,
                                     0,
                                     1,
                                     "generates a 2000000001 Hz carrier on any board (the limit is 2000000000 Hz)"},
            PllDividerChoiceTestCase{"RefusedWithoutASetting",
                                     true,
                                     1'296'000'000,
                                     10'000,
                                     -1,
                                     8,
                                     "generates a 1296000000 Hz carrier on this board"},
            PllDividerChoiceTestCase{"RefusedBelowTheMinimumRate",
                                     true,
                                     50'000,
                                     0,
                                     0,
                                     4095,
                                     "the PLL would run at 409500000 Hz, below its 600000000 Hz minimum); use a "
                                     "higher carrier frequency"},
            PllDividerChoiceTestCase{"RefusedWithAGuardBandAsWideAsTheSteps",
                                     true,
                                     2'500'000,
                                     225'000,
                                     0,
                                     103,
                                     "the PLL would run at 515000000 Hz, below its 600000000 Hz minimum); use a "
                                     "higher carrier frequency"},
            PllDividerChoiceTestCase{"RefusedWithAGuardBandThatFitsBelowTheMinimumRateOnly",
                                     true,
                                     2'510'460,
                                     226'000,
                                     0,
                                     103,
                                     "the PLL would run at 517154760 Hz, below its 600000000 Hz minimum); use a "
                                     "higher carrier frequency"},
            PllDividerChoiceTestCase{"RefusedWithAGuardBandNarrowerThanTheSteps",
                                     true,
                                     2'500'000,
                                     224'999,
                                     0,
                                     103,
                                     "the carrier lies next to an integer step of the PLL multiplier for every "
                                     "divider that runs the PLL at 600000000 Hz or above); use another carrier "
                                     "frequency"},
            PllDividerChoiceTestCase{"RefusedOnAnIntegerStepOnBcm2711",
                                     true,
                                     108'000'000,
                                     48'000,
                                     0,
                                     2,
                                     "the carrier lies next to an integer step of the PLL multiplier for every "
                                     "divider that runs the PLL at 600000000 Hz or above); use another carrier "
                                     "frequency"},
            PllDividerChoiceTestCase{"RefusedOnAnIntegerStepOnBcm2835",
                                     false,
                                     96'000'000,
                                     228'000,
                                     0,
                                     3,
                                     "the carrier lies next to an integer step of the PLL multiplier for every "
                                     "divider that runs the PLL at 600000000 Hz or above); use another carrier "
                                     "frequency"},
        };
    }
}  // namespace

class PllDividerChoiceTest : public ::testing::TestWithParam<PllDividerChoiceTestCase> {};

/**
 * @brief The divider choice accepts the runnable settings and refuses the others with the diagnostic.
 */
TEST_P(PllDividerChoiceTest, RefusesTheSettingsTheBoardCannotRun) {
    const auto& testCase{GetParam()};
    FakeClock fakeClock{.pi_is_2711 = testCase.pi4, .result = testCase.result, .divider = testCase.divider};

    if (testCase.expectedError.empty()) {
        EXPECT_NO_THROW(choosePllDivider(fakeClock, testCase.frequency, testCase.guardBand, "subject", ""));
        return;
    }
    try {
        choosePllDivider(fakeClock, testCase.frequency, testCase.guardBand, "subject", "");
        FAIL() << "the setting was accepted";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string{error.what()}.find(testCase.expectedError), std::string::npos) << error.what();
    }
}

/**
 * @brief ComputeBestLO gets the PLL frequency (twice the carrier on BCM2711) and the guard band unchanged.
 */
TEST_P(PllDividerChoiceTest, PassesThePllFrequencyAndTheGuardBand) {
    const auto& testCase{GetParam()};
    FakeClock fakeClock{.pi_is_2711 = testCase.pi4, .result = testCase.result, .divider = testCase.divider};

    try {
        choosePllDivider(fakeClock, testCase.frequency, testCase.guardBand, "subject", "");
    } catch (const std::runtime_error&) {
    }

    if (testCase.frequency > MAX_CARRIER_FREQUENCY_HZ) {
        EXPECT_EQ(fakeClock.requestedGuardBand, -1) << "ComputeBestLO must not run above the limit";
        return;
    }
    EXPECT_EQ(fakeClock.requestedFrequency, testCase.frequency * (testCase.pi4 ? 2U : 1U));
    EXPECT_EQ(fakeClock.requestedGuardBand, testCase.guardBand);
}

INSTANTIATE_TEST_SUITE_P(TransmitterCarriers, PllDividerChoiceTest,
                         ::testing::ValuesIn(makePllDividerChoiceTestCases()),
                         [](const ::testing::TestParamInfo<PllDividerChoiceTestCase>& info) {
                             return std::string{info.param.name};
                         });

/**
 * @brief The diagnostic of a refused rate names the setting and the advice the caller passed.
 */
TEST(PllDividerChoiceMessageTest, NamesTheSubjectAndTheAdvice) {
    FakeClock fakeClock{.pi_is_2711 = true, .divider = 103};

    try {
        choosePllDivider(fakeClock, 2'500'000, 228'000, "sweeps a 456000 Hz band at 2500000 Hz", " or a smaller band");
        FAIL() << "the setting was accepted";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(),
                     "no PLL setting sweeps a 456000 Hz band at 2500000 Hz on this board (the PLL would run at "
                     "515000000 Hz, below its 600000000 Hz minimum); use a higher carrier frequency or a smaller band");
    }
}

namespace {
    struct PllRateTestCase {
        std::string_view name;
        uint64_t pllFrequency;
        int divider;
        uint64_t expectedRateHz;
    };

    /**
     * @brief Render a PllRateTestCase as its name, PLL frequency and divider for gtest listings and
     *        failure messages.
     */
    void PrintTo(const PllRateTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {pllFrequency=" << testCase.pllFrequency << ", divider=" << testCase.divider << "}";
    }

    /**
     * @brief The PLL settings librpitx chooses for the carriers of the transmitters.
     */
    std::vector<PllRateTestCase> makePllRateTestCases() {
        return {
            // 434 MHz on BCM2711: the pre-divider doubles the rate of the PLL.
            PllRateTestCase{"PredividerDoublesTheRate", 868'000'000, 1, 1'736'000'000},
            // 434 MHz on BCM2835: the PLL runs at the carrier times the divider.
            PllRateTestCase{"DividerMultipliesTheCarrier", 434'000'000, 3, 1'302'000'000},
            // 100 kHz on BCM2711: the largest divider still keeps the PLL above its minimum.
            PllRateTestCase{"LargestDividerAtLowCarrier", 200'000, 4095, 819'000'000},
            // 50 kHz on BCM2711: even the largest divider runs the PLL below its minimum.
            PllRateTestCase{"BelowMinimumAtLowCarrier", 100'000, 4095, 409'500'000},
        };
    }
}  // namespace

class PllRateTest : public ::testing::TestWithParam<PllRateTestCase> {};

/**
 * @brief The PLL rate follows the divider, with divider 1 standing for the pre-divider.
 */
TEST_P(PllRateTest, FollowsTheDivider) {
    const auto& testCase{GetParam()};

    EXPECT_EQ(pllRateHz(testCase.pllFrequency, testCase.divider), testCase.expectedRateHz);
}

INSTANTIATE_TEST_SUITE_P(CarrierSettings, PllRateTest, ::testing::ValuesIn(makePllRateTestCases()),
                         [](const ::testing::TestParamInfo<PllRateTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct CarrierStepTestCase {
        std::string_view name;
        bool pi4;
        uint64_t crystalFrequency;
        int divider;
        double expectedStepHz;
    };

    /**
     * @brief Render a CarrierStepTestCase as its name, divider and expected step for gtest listings
     *        and failure messages.
     */
    void PrintTo(const CarrierStepTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {divider=" << testCase.divider << ", expectedStepHz=" << testCase.expectedStepHz
            << "}";
    }

    /**
     * @brief The four ways clkgpio::GetMasterFrac turns a carrier into the PLL multiplier: with the
     *        pre-divider (divider 1) and with a plain divider, on the 54 MHz crystal of BCM2711
     *        and on the 19.2 MHz crystal of the earlier SoCs.
     */
    std::vector<CarrierStepTestCase> makeCarrierStepTestCases() {
        return {
            // 434 MHz on BCM2711: multiplier = 2 * carrier / crystal.
            CarrierStepTestCase{"PredividerOnBcm2711", true, 54'000'000, 1, 54.0e6 / 2.0 / 1'048'576.0},
            // 144.5 MHz on BCM2711: multiplier = 2 * 5 * carrier / crystal.
            CarrierStepTestCase{"DividerOnBcm2711", true, 54'000'000, 5, 54.0e6 / 10.0 / 1'048'576.0},
            // 868 MHz on BCM2835: multiplier = carrier / (2 * crystal).
            CarrierStepTestCase{"PredividerOnBcm2835", false, 19'200'000, 1, 19.2e6 * 2.0 / 1'048'576.0},
            // 434 MHz on BCM2835: multiplier = 3 * carrier / crystal.
            CarrierStepTestCase{"DividerOnBcm2835", false, 19'200'000, 3, 19.2e6 / 3.0 / 1'048'576.0},
        };
    }
}  // namespace

class CarrierStepTest : public ::testing::TestWithParam<CarrierStepTestCase> {};

/**
 * @brief The carrier step is the crystal frequency over the multiplier scale and the 2^20 fraction units:
 *        25.7 Hz and 5.1 Hz on BCM2711, 36.6 Hz and 6.1 Hz on BCM2835 for the settings above.
 */
TEST_P(CarrierStepTest, FollowsTheMultiplierOfTheSetting) {
    const auto& testCase{GetParam()};
    const FakeClock fakeClock{
        .pi_is_2711 = testCase.pi4, .XOSC_FREQUENCY = testCase.crystalFrequency, .PllFixDivider = testCase.divider};

    EXPECT_DOUBLE_EQ(carrierStepHz(fakeClock), testCase.expectedStepHz);
}

INSTANTIATE_TEST_SUITE_P(PllSettings, CarrierStepTest, ::testing::ValuesIn(makeCarrierStepTestCases()),
                         [](const ::testing::TestParamInfo<CarrierStepTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct IntegerStepCrossingTestCase {
        std::string_view name;
        bool pi4;
        int divider;
        double carrier;
        double clockCorrectionPpm;
        double lowestDeviation;
        double highestDeviation;
        bool expectedCrossing;
    };

    /**
     * @brief Render an IntegerStepCrossingTestCase as its name, carrier, range and expected outcome for
     *        gtest listings and failure messages.
     */
    void PrintTo(const IntegerStepCrossingTestCase& testCase, std::ostream* os) {
        *os << std::boolalpha << testCase.name << " {carrier=" << testCase.carrier << ", range=["
            << testCase.lowestDeviation << ", " << testCase.highestDeviation
            << "], expectedCrossing=" << testCase.expectedCrossing << "}";
    }

    /**
     * @brief Ranges around the integer steps of the four multiplier forms: with the pre-divider the
     *        steps lie 27 MHz apart on BCM2711 (432 MHz is one) and 38.4 MHz apart on BCM2835 (768 MHz),
     *        with divider 2 13.5 MHz (216 MHz) and 9.6 MHz (307.2 MHz) apart. A clock correction of
     *        20 ppm moves the step at 432 MHz down by 8640 Hz.
     */
    std::vector<IntegerStepCrossingTestCase> makeIntegerStepCrossingTestCases() {
        return {
            IntegerStepCrossingTestCase{"BandBelowTheStep", true, 1, 431'994'000.0, 0.0, -5'000.0, 5'000.0, false},
            IntegerStepCrossingTestCase{"BandAcrossTheStep", true, 1, 431'996'000.0, 0.0, -5'000.0, 5'000.0, true},
            IntegerStepCrossingTestCase{"BandAboveTheStep", true, 1, 432'006'000.0, 0.0, -5'000.0, 5'000.0, false},
            IntegerStepCrossingTestCase{"CarrierOnTheStep", true, 1, 432'000'000.0, 0.0, -5'000.0, 5'000.0, true},
            // The sample on the step itself belongs to the next integer.
            IntegerStepCrossingTestCase{
                "HighestDeviationOnTheStep", true, 1, 431'995'000.0, 0.0, -5'000.0, 5'000.0, true},
            IntegerStepCrossingTestCase{"RangeUpFromTheStep", true, 1, 432'000'000.0, 0.0, 0.0, 2'300.0, false},
            // An unmodulated carrier crosses no step, not even the one it sits on.
            IntegerStepCrossingTestCase{"RangeOfNoWidth", true, 1, 432'000'000.0, 0.0, 0.0, 0.0, false},
            IntegerStepCrossingTestCase{"DividerOnBcm2711", true, 2, 216'003'000.0, 0.0, -5'000.0, 5'000.0, true},
            IntegerStepCrossingTestCase{"PredividerOnBcm2835", false, 1, 767'996'000.0, 0.0, -5'000.0, 5'000.0, true},
            IntegerStepCrossingTestCase{"DividerOnBcm2835", false, 2, 307'202'000.0, 0.0, -5'000.0, 5'000.0, true},
            // The corrected step lies at 431 991 360 Hz: inside the first band, below the second one.
            IntegerStepCrossingTestCase{
                "StepMovedIntoTheBandByTheClockCorrection", true, 1, 431'990'000.0, 20.0, -5'000.0, 5'000.0, true},
            IntegerStepCrossingTestCase{
                "StepMovedOutOfTheBandByTheClockCorrection", true, 1, 431'998'000.0, 20.0, -5'000.0, 5'000.0, false},
            // From one integer step to the next the fraction comes back to its start without a wrap.
            IntegerStepCrossingTestCase{
                "RangeAsWideAsTheStepsOnBcm2711", true, 1, 445'500'000.0, 0.0, -13'500'000.0, 13'500'000.0, true},
            IntegerStepCrossingTestCase{
                "RangeNarrowerThanTheStepsOnBcm2711", true, 1, 445'500'000.0, 0.0, -13'400'000.0, 13'400'000.0, false},
            IntegerStepCrossingTestCase{
                "RangeAsWideAsTheStepsOnBcm2835", false, 2, 312'000'000.0, 0.0, -4'800'000.0, 4'800'000.0, true},
            IntegerStepCrossingTestCase{
                "RangeNarrowerThanTheStepsOnBcm2835", false, 2, 312'000'000.0, 0.0, -4'700'000.0, 4'700'000.0, false},
        };
    }
}  // namespace

class IntegerStepCrossingTest : public ::testing::TestWithParam<IntegerStepCrossingTestCase> {};

/**
 * @brief A range of deviations crosses an integer step when the step lies inside it, wherever the
 *        clock correction has moved the step, or when the range is as wide as the distance between two steps.
 */
TEST_P(IntegerStepCrossingTest, FindsTheStepInsideTheRange) {
    const auto& testCase{GetParam()};
    FakeClock fakeClock{.pi_is_2711         = testCase.pi4,
                        .PllFixDivider      = testCase.divider,
                        .carrier            = testCase.carrier,
                        .clockCorrectionPpm = testCase.clockCorrectionPpm};

    EXPECT_EQ(crossesIntegerStep(fakeClock, testCase.lowestDeviation, testCase.highestDeviation),
              testCase.expectedCrossing);
}

INSTANTIATE_TEST_SUITE_P(RangesAroundTheSteps, IntegerStepCrossingTest,
                         ::testing::ValuesIn(makeIntegerStepCrossingTestCases()),
                         [](const ::testing::TestParamInfo<IntegerStepCrossingTestCase>& info) {
                             return std::string{info.param.name};
                         });
