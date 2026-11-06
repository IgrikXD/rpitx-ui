/**
 * @file test_flush_to_zero.cpp
 * @brief Unit tests for the flush-to-zero floating-point mode switch.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 06.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <bit>
#include <cfenv>
#include <cstdint>
#include <limits>

#include "biquad.h"
#include "flush_to_zero.h"

namespace {
    /**
     * @brief Smallest normal float (2^-126) - any smaller non-zero magnitude is subnormal.
     */
    constexpr float kMinNormal{std::numeric_limits<float>::min()};

    /**
     * @brief Bit pattern of kMinNormal - the lower bound of the normal magnitudes.
     */
    constexpr std::uint32_t kMinNormalBits{std::bit_cast<std::uint32_t>(kMinNormal)};

    /**
     * @brief Bit pattern of value without the sign bit.
     *
     * Results are inspected through their bits: in flush-to-zero mode a floating-point
     * comparison may read a subnormal operand as zero and then cannot tell the two apart.
     */
    std::uint32_t magnitudeBits(float value) {
        constexpr std::uint32_t kMagnitudeMask{0x7FFF'FFFFU};
        return std::bit_cast<std::uint32_t>(value) & kMagnitudeMask;
    }

    /**
     * @brief Multiply under the floating-point mode that is active during the call.
     *
     * The compiler is not aware of mode changes: it may fold the product at build time or
     * move the multiplication past a later mode switch. Passing the operands and the product
     * through volatiles pins the multiplication to this call.
     */
    float multiplyAtRuntime(float left, float right) {
        volatile float runtimeLeft{left};
        volatile float runtimeRight{right};
        volatile float runtimeProduct{runtimeLeft * runtimeRight};
        return runtimeProduct;
    }

    /**
     * @brief Count the subnormal outputs of the pifmrds DC blocker (30 Hz high-pass at the
     *        228 kHz MPX rate) after a unit impulse followed by one second of silence.
     *
     * The impulse and the count pass through volatiles for the same reason as the operands
     * and the product of multiplyAtRuntime().
     */
    int countSubnormalTailSamples() {
        constexpr float kCutoffHz{30.0F};
        constexpr float kSampleRate{228'000.0F};
        constexpr int kSilenceSamples{228'000};
        volatile float runtimeImpulse{1.0F};

        Biquad biquad{Biquad::highPass(kCutoffHz, kSampleRate)};
        [[maybe_unused]] const float impulseSample{biquad.process(runtimeImpulse)};

        int subnormalSamples{0};
        for (int n{0}; n < kSilenceSamples; ++n) {
            const std::uint32_t tailMagnitude{magnitudeBits(biquad.process(0.0F))};
            if (tailMagnitude != 0 && tailMagnitude < kMinNormalBits) {
                ++subnormalSamples;
            }
        }
        volatile int runtimeCount{subnormalSamples};
        return runtimeCount;
    }
}  // namespace

/**
 * @brief Restores the floating-point environment of the test thread after each test, so
 *        the mode enabled by one test does not leak into the next one.
 */
class EnableFlushToZeroTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(std::fegetenv(&savedEnvironment_), 0);
    }

    void TearDown() override {
        EXPECT_EQ(std::fesetenv(&savedEnvironment_), 0);
    }

private:
    std::fenv_t savedEnvironment_{};
};

/**
 * @brief A product that falls below the normal range becomes zero instead of a subnormal.
 *
 * kMinNormal * 0.5 = 2^-127 is exactly representable as the subnormal 0x00400000, so the
 * default mode must return that bit pattern - the leading assertion proves that the target
 * underflows gradually and that the mode change is observable at all. After
 * enableFlushToZero() the same product must carry no magnitude bits; an implementation
 * that sets a neighbouring control bit, or none, still returns 0x00400000.
 */
TEST_F(EnableFlushToZeroTest, SubnormalResultIsFlushedToZero) {
    constexpr float kHalf{0.5F};
    constexpr std::uint32_t kHalfMinNormalBits{0x0040'0000U};

    ASSERT_EQ(magnitudeBits(multiplyAtRuntime(kMinNormal, kHalf)), kHalfMinNormalBits);

    enableFlushToZero();

    EXPECT_EQ(magnitudeBits(multiplyAtRuntime(kMinNormal, kHalf)), 0U);
}

/**
 * @brief A subnormal operand is read as zero, even when the exact result would be normal.
 *
 * The smallest subnormal (2^-149) times 2^24 is 2^-125 - a normal number, which the default
 * mode returns exactly (leading assertion). In flush-to-zero mode the operand is replaced
 * by zero before the multiplication, so the product is zero. This covers the subnormals
 * that the thread did not compute itself, e.g. samples decoded from a float audio file:
 * a mode that only flushed results would still return 2^-125 here.
 */
TEST_F(EnableFlushToZeroTest, SubnormalOperandIsReadAsZero) {
    constexpr float kMinSubnormal{std::numeric_limits<float>::denorm_min()};
    constexpr float kNormalizingFactor{16'777'216.0F};         // 2^24
    constexpr std::uint32_t kNormalProductBits{0x0100'0000U};  // 2^-125

    ASSERT_EQ(magnitudeBits(multiplyAtRuntime(kMinSubnormal, kNormalizingFactor)), kNormalProductBits);

    enableFlushToZero();

    EXPECT_EQ(magnitudeBits(multiplyAtRuntime(kMinSubnormal, kNormalizingFactor)), 0U);
}

/**
 * @brief Enabling the mode does not disturb the rounding mode of the thread.
 *
 * The rounding mode lives in the same control register as the flush-to-zero bit, and a
 * change to it would alter every DSP result. FE_UPWARD is selected first because it is not
 * the default: an implementation that overwrites the register instead of adding the bit
 * falls back to round-to-nearest and fails the final check.
 */
TEST_F(EnableFlushToZeroTest, RoundingModeIsPreserved) {
    ASSERT_EQ(std::fesetround(FE_UPWARD), 0);

    enableFlushToZero();

    EXPECT_EQ(std::fegetround(), FE_UPWARD);
}

/**
 * @brief The decaying tail of a silent filter never enters the subnormal range.
 *
 * Reproduces the cause of the silence slowdown: after a unit impulse the tail of the pifmrds
 * DC blocker (pole radius ~0.9994 per sample) first drops below kMinNormal after ~126 000
 * samples of silence and stays there from ~136 000 samples on. In the default mode about
 * 92 000 of the 228 000 outputs are therefore subnormal - the leading assertion, which also
 * proves that the run is long enough to expose the defect. In flush-to-zero mode not a
 * single output of the same run may be subnormal.
 */
TEST_F(EnableFlushToZeroTest, SilentFilterTailStaysOutOfSubnormalRange) {
    ASSERT_GT(countSubnormalTailSamples(), 0);

    enableFlushToZero();

    EXPECT_EQ(countSubnormalTailSamples(), 0);
}
