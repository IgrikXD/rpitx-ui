/**
 * @file test_carrier_pll.cpp
 * @brief Unit tests for the PLL rate of the carrier clock settings.
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
#include <string>
#include <string_view>
#include <vector>

#include "carrier_pll.h"

namespace {
    struct PllRateTestCase {
        std::string_view name;
        uint64_t pllFrequency;
        int divider;
        double expectedRateHz;
        bool expectedRunnable;
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
            PllRateTestCase{"PredividerDoublesTheRate", 868'000'000, 1, 1'736'000'000.0, true},
            // 434 MHz on BCM2835: the PLL runs at the carrier times the divider.
            PllRateTestCase{"DividerMultipliesTheCarrier", 434'000'000, 3, 1'302'000'000.0, true},
            // 100 kHz on BCM2711: the largest divider still keeps the PLL above its minimum.
            PllRateTestCase{"LargestDividerAtLowCarrier", 200'000, 4095, 819'000'000.0, true},
            // 50 kHz on BCM2711: even the largest divider runs the PLL below its minimum.
            PllRateTestCase{"BelowMinimumAtLowCarrier", 100'000, 4095, 409'500'000.0, false},
        };
    }
}  // namespace

class PllRateTest : public ::testing::TestWithParam<PllRateTestCase> {};

/**
 * @brief The PLL rate follows the divider, with divider 1 standing for the pre-divider.
 */
TEST_P(PllRateTest, FollowsTheDivider) {
    const auto& testCase{GetParam()};

    EXPECT_DOUBLE_EQ(pllRateHz(testCase.pllFrequency, testCase.divider), testCase.expectedRateHz);
}

/**
 * @brief The minimum rate of the PLL separates the runnable settings from the ones that freeze the board.
 */
TEST_P(PllRateTest, MinimumRateSeparatesRunnableSettings) {
    const auto& testCase{GetParam()};

    EXPECT_EQ(pllRateHz(testCase.pllFrequency, testCase.divider) >= MIN_PLL_RATE_HZ, testCase.expectedRunnable);
}

INSTANTIATE_TEST_SUITE_P(CarrierSettings, PllRateTest, ::testing::ValuesIn(makePllRateTestCases()),
                         [](const ::testing::TestParamInfo<PllRateTestCase>& info) {
                             return std::string{info.param.name};
                         });
