/**
 * @file test_iq_sample.cpp
 * @brief Unit tests for the IqSample value type.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 05.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "iq_sample.h"

namespace {
    struct IqSampleTestCase {
        std::string_view name;
        IqSample createdIqSample;
        float expectedI;
        float expectedQ;
    };

    /**
     * @brief Render an IqSampleTestCase as its human-readable name and expected I/Q values
     *        for gtest listings and failure messages.
     */
    void PrintTo(const IqSampleTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {i=" << testCase.expectedI << ", q=" << testCase.expectedQ << "}";
    }

    /**
     * @brief Build the parametric test cases covering IqSample construction variants.
     */
    std::vector<IqSampleTestCase> makeIqSampleTestCases() {
        return {
            IqSampleTestCase{"DefaultZeroes", IqSample{}, 0.0F, 0.0F},
            IqSampleTestCase{"PositiveAndNegative", IqSample{.i = 0.25F, .q = -0.5F}, 0.25F, -0.5F},
            IqSampleTestCase{"BothNegative", IqSample{.i = -0.125F, .q = -0.875F}, -0.125F, -0.875F},
            IqSampleTestCase{
                "HighPrecisionMixedSigns", IqSample{.i = 1.234567F, .q = -7.654321F}, 1.234567F, -7.654321F},
            IqSampleTestCase{"WideMagnitudeRange", IqSample{.i = 1.0e-6F, .q = -1.0e3F}, 1.0e-6F, -1.0e3F},
        };
    }
}  // namespace

class IqSampleTest : public ::testing::TestWithParam<IqSampleTestCase> {};

/**
 * @brief Verify that the constructed IqSample carries the expected I/Q components.
 */
TEST_P(IqSampleTest, StoresProvidedComponents) {
    const auto& testCase{GetParam()};

    EXPECT_FLOAT_EQ(testCase.createdIqSample.i, testCase.expectedI);
    EXPECT_FLOAT_EQ(testCase.createdIqSample.q, testCase.expectedQ);
}

INSTANTIATE_TEST_SUITE_P(ConstructionVariants, IqSampleTest, ::testing::ValuesIn(makeIqSampleTestCases()),
                         [](const ::testing::TestParamInfo<IqSampleTestCase>& info) {
                             return std::string{info.param.name};
                         });
