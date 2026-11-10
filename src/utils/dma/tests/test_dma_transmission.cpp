/**
 * @file test_dma_transmission.cpp
 * @brief Unit tests for the drain tail queued before a librpitx DMA engine stops.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 10.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "dma_transmission.h"

namespace {
    /**
     * @brief Frequency deviation that keeps an FM carrier unmodulated.
     */
    constexpr float IDLE_DEVIATION_HZ{0.0F};

    struct DrainTailTestCase {
        std::string_view name;
        uint32_t fifoSize;
    };

    /**
     * @brief Render a DrainTailTestCase as its name and ring size for gtest listings and
     *        failure messages.
     */
    void PrintTo(const DrainTailTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {fifoSize=" << testCase.fifoSize << "}";
    }

    /**
     * @brief The ring sizes the transmitters are built with.
     */
    std::vector<DrainTailTestCase> makeDrainTailTestCases() {
        return {
            DrainTailTestCase{"Ring4096", 4096},
            DrainTailTestCase{"Ring8192", 8192},
            DrainTailTestCase{"Ring16384", 16384},
        };
    }
}  // namespace

class DrainTailTest : public ::testing::TestWithParam<DrainTailTestCase> {};

/**
 * @brief The tail arrives as two pushes of half a ring each, every sample equal to the idle one.
 *
 * Two pushes of fifoSize / 2 samples are the contract the drain relies on: the first half
 * exceeds the quarter ring that starts the engine without wrapping onto a short input, and
 * the two halves together cover one full ring, so every earlier sample has been played
 * when the second push returns.
 */
TEST_P(DrainTailTest, QueuesOneRingOfIdleSamplesInTwoHalves) {
    const auto& testCase{GetParam()};
    std::vector<std::size_t> pushedSizes;
    std::size_t idleSamples{0};

    queueDrainTail(testCase.fifoSize, IDLE_DEVIATION_HZ, [&pushedSizes, &idleSamples](std::vector<float>& tail) {
        pushedSizes.push_back(tail.size());
        idleSamples += static_cast<std::size_t>(std::count(tail.begin(), tail.end(), IDLE_DEVIATION_HZ));
    });

    const std::size_t halfRing{testCase.fifoSize / 2};
    EXPECT_EQ(pushedSizes, (std::vector<std::size_t>{halfRing, halfRing}));
    EXPECT_EQ(idleSamples, 2 * halfRing);
}

INSTANTIATE_TEST_SUITE_P(RingSizes, DrainTailTest, ::testing::ValuesIn(makeDrainTailTestCases()),
                         [](const ::testing::TestParamInfo<DrainTailTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    /**
     * @brief IQ sample whose zero magnitude switches the carrier off.
     */
    constexpr std::complex<float> CARRIER_OFF_SAMPLE{0.0F, 0.0F};
}  // namespace

/**
 * @brief The tail carries the idle sample of any sample type, here the carrier-off IQ sample.
 *
 * iqdmasync switches the carrier off for a magnitude below 1/8 and on for anything above, so
 * the IQ drain must fill its tail with exactly {0, 0}; a tail sample of magnitude 1/8 or more
 * would key the carrier for the duration of the tail.
 */
TEST(DrainTailIqTest, FillsTheTailWithTheIdleIqSample) {
    constexpr uint32_t ringSize{4096};
    constexpr std::size_t expectedPushes{2};
    std::size_t pushes{0};
    bool allCarrierOff{true};

    queueDrainTail(ringSize, CARRIER_OFF_SAMPLE, [&pushes, &allCarrierOff](std::vector<std::complex<float>>& tail) {
        ++pushes;
        allCarrierOff = allCarrierOff && std::all_of(tail.begin(), tail.end(), [](std::complex<float> sample) {
                            return sample == CARRIER_OFF_SAMPLE;
                        });
    });

    EXPECT_EQ(pushes, expectedPushes);
    EXPECT_TRUE(allCarrierOff);
}
