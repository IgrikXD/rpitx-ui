/**
 * @file test_soxr_resampler.cpp
 * @brief Unit tests for the SoxrResampler RAII wrapper.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 12.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "soxr_resampler.h"

namespace {
    /**
     * @brief CD-audio source rate (44.1 kHz). Default upsampling source for the
     *        process()/clear()/move tests and several acceptance cases.
     */
    constexpr int kCdAudioRateHz{44'100};

    /**
     * @brief Broadcast target rate (48 kHz). Paired with kCdAudioRateHz on the
     *        upsampling path and reused as both source and target on the
     *        identity-ratio acceptance case.
     */
    constexpr int kBroadcastRateHz{48'000};

    /**
     * @brief Lower extreme rate used by the extreme-upsample acceptance case
     *        (8 kHz -> 192 kHz). Exercises a wide rate ratio that pushes the
     *        libsoxr filter dimensions toward their upper bound.
     */
    constexpr int kNarrowRateHz{8'000};

    /**
     * @brief Upper extreme rate paired with kNarrowRateHz on the same case.
     */
    constexpr int kVeryWideRateHz{192'000};

    /**
     * @brief Default per-block input size used across the EOS-flush, clear, and move tests at
     *        the canonical CD -> Broadcast rate pair. Matches the per-block sizes
     *        pinfm/pifmrds feed in production.
     */
    constexpr std::size_t kBlockInputFrames{1024};

    /**
     * @brief Output buffer size paired with kBlockInputFrames at the 44.1 -> 48 kHz ratio.
     *        ~2x the expected output count (1024 * 48000 / 44100 ~= 1114) so libsoxr halts
     *        on input exhaustion rather than the output cap.
     */
    constexpr std::size_t kBlockOutputFrames{2048};
}  // namespace

/**
 * @brief process() consumes the full input span when the output buffer is generously sized.
 *
 * With 2048 input frames at the 44.1 -> 48 kHz ratio the expected output is ~2230 frames;
 * the 4096-frame staging buffer is roughly 2x that, so libsoxr halts on input exhaustion
 * (inputConsumed == in.size()) rather than the output cap. The combined assertions verify
 * the full-consume invariant, that the call returned a present optional, and that
 * outputProduced is non-zero (catches a "no-op" implementation) yet stays within the
 * supplied output buffer.
 */
TEST(SoxrResamplerTest, ProcessConsumesFullInputWhenStagingOversized) {
    SoxrResampler soxrResampler{kCdAudioRateHz, kBroadcastRateHz};
    constexpr std::size_t kInputFrames{2048};
    constexpr std::size_t kOversizedOutputFrames{4096};
    std::vector<float> inputSamples(kInputFrames, 0.5F);
    std::vector<float> outputSamples(kOversizedOutputFrames, 0.0F);

    const auto result{soxrResampler.process(inputSamples, outputSamples)};

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().inputConsumed, inputSamples.size());
    EXPECT_GT(result.value().outputProduced, 0U);
    EXPECT_LE(result.value().outputProduced, outputSamples.size());
}

/**
 * @brief Empty input span as the EOS flush form succeeds and reports zero input consumed.
 *
 * Priming the resampler with a non-empty block first populates the filter delay line so
 * the subsequent empty-input call exercises libsoxr's documented end-of-stream flush form.
 * The call must succeed (non-nullopt) and report inputConsumed == 0 because no input was
 * supplied. outputProduced is intentionally not asserted - the residual tail length depends
 * on internal libsoxr state and is not a stable contract.
 */
TEST(SoxrResamplerTest, EmptyInputFlushReportsZeroConsumed) {
    SoxrResampler soxrResampler{kCdAudioRateHz, kBroadcastRateHz};
    std::vector<float> primingInput(kBlockInputFrames, 0.5F);
    std::vector<float> primingOutput(kBlockOutputFrames, 0.0F);
    ASSERT_TRUE(soxrResampler.process(primingInput, primingOutput).has_value());

    constexpr std::size_t kFlushOutputFrames{512};
    std::vector<float> flushOutput(kFlushOutputFrames, 0.0F);
    const auto result{soxrResampler.process({}, flushOutput)};

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().inputConsumed, 0U);
}

/**
 * @brief After clear(), the resampler produces the same first-call output count as a fresh instance.
 *
 * Builds two resamplers with identical rates. The reference instance processes one block from
 * a cold start, so its outputProduced reflects libsoxr's warmup-pass output (smaller than
 * steady-state because the filter delay line has not yet been populated). The subject
 * instance is primed with one block (filling the delay line), then clear()ed, then processes
 * the same input. A buggy no-op clear() would leave the delay line populated and the second
 * pass would emit the steady-state (larger) output count; a working clear() restores the
 * delay line to its empty state so the post-clear call matches the reference warmup pass
 * exactly. Comparing outputProduced is the smallest stable signal that the internal state
 * was actually reset - per-sample buffer equality would be a stricter contract but relies
 * on libsoxr's float output being bit-stable across platforms, which is documented behavior
 * but not contractually guaranteed.
 */
TEST(SoxrResamplerTest, ClearProducesFreshInstanceOutputCount) {
    std::vector<float> inputSamples(kBlockInputFrames, 0.5F);

    SoxrResampler freshResampler{kCdAudioRateHz, kBroadcastRateHz};
    std::vector<float> freshOutput(kBlockOutputFrames, 0.0F);
    const auto freshResult{freshResampler.process(inputSamples, freshOutput)};
    ASSERT_TRUE(freshResult.has_value());

    SoxrResampler primedAndClearedResampler{kCdAudioRateHz, kBroadcastRateHz};
    std::vector<float> primingOutput(kBlockOutputFrames, 0.0F);
    ASSERT_TRUE(primedAndClearedResampler.process(inputSamples, primingOutput).has_value());
    primedAndClearedResampler.clear();
    std::vector<float> postClearOutput(kBlockOutputFrames, 0.0F);
    const auto postClearResult{primedAndClearedResampler.process(inputSamples, postClearOutput)};
    ASSERT_TRUE(postClearResult.has_value());

    EXPECT_EQ(postClearResult.value().outputProduced, freshResult.value().outputProduced);
}

/**
 * @brief Move-constructed instance retains a fully functional libsoxr handle.
 *
 * After the move the donor must remain safely destructible at scope exit (verified implicitly
 * by the test completing without crash) and the taker must carry the working handle. A present
 * optional from process() is the smallest signal that the underlying soxr_t survived the move
 * and the resampler is still wired through to libsoxr.
 */
TEST(SoxrResamplerTest, MoveConstructedInstanceProcessesSuccessfully) {
    SoxrResampler donor{kCdAudioRateHz, kBroadcastRateHz};
    SoxrResampler taker{std::move(donor)};
    std::vector<float> inputSamples(kBlockInputFrames, 0.5F);
    std::vector<float> outputSamples(kBlockOutputFrames, 0.0F);

    EXPECT_TRUE(taker.process(inputSamples, outputSamples).has_value());
}

/**
 * @brief Move-assigned instance retains a fully functional libsoxr handle and adopts donor's rates.
 *
 * The taker is intentionally constructed with the reversed rate pair so the assignment is
 * non-trivial - it must release the old handle and adopt donor's. The strong-form contract
 * is that taker's post-assignment process() emits the same outputProduced as a cold-start
 * reference resampler built at donor's rates running on the same input - a buggy operator=
 * that left taker holding its original 48 -> 44.1 kHz handle would emit the downsampling
 * output count instead and fail the comparison. Reference-relative comparison side-steps
 * libsoxr's cold-start filter warmup, which would make an absolute frame-count assertion
 * (e.g. outputProduced > inputSamples.size()) unreliable at Medium quality because the
 * warmup deficit can erase the upsampling gain on a single 1024-sample block.
 */
TEST(SoxrResamplerTest, MoveAssignedInstanceProcessesSuccessfully) {
    std::vector<float> inputSamples(kBlockInputFrames, 0.5F);

    SoxrResampler referenceResampler{kCdAudioRateHz, kBroadcastRateHz};
    std::vector<float> referenceOutput(kBlockOutputFrames, 0.0F);
    const auto referenceResult{referenceResampler.process(inputSamples, referenceOutput)};
    ASSERT_TRUE(referenceResult.has_value());

    SoxrResampler donor{kCdAudioRateHz, kBroadcastRateHz};
    SoxrResampler taker{kBroadcastRateHz, kCdAudioRateHz};
    taker = std::move(donor);
    std::vector<float> takerOutput(kBlockOutputFrames, 0.0F);
    const auto takerResult{taker.process(inputSamples, takerOutput)};
    ASSERT_TRUE(takerResult.has_value());

    EXPECT_EQ(takerResult.value().outputProduced, referenceResult.value().outputProduced);
}

namespace {
    struct CtorRateRejectionTestCase {
        std::string_view name;
        int sourceRateHz;
        int targetRateHz;
    };

    /**
     * @brief Render a ctor rate-rejection case as its name plus the offending rate pair for
     *        gtest listings and failure messages.
     */
    void PrintTo(const CtorRateRejectionTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {sourceRateHz=" << testCase.sourceRateHz << ", targetRateHz=" << testCase.targetRateHz
            << "}";
    }

    /**
     * @brief Constructor rate pairs that violate the (sourceRateHz > 0, targetRateHz > 0)
     *        precondition. One named case per distinct violation channel.
     */
    std::vector<CtorRateRejectionTestCase> makeCtorRateRejectionTestCases() {
        return {
            CtorRateRejectionTestCase{"SourceRateZero", 0, kBroadcastRateHz},
            CtorRateRejectionTestCase{"SourceRateNegative", -1, kBroadcastRateHz},
            CtorRateRejectionTestCase{"TargetRateZero", kBroadcastRateHz, 0},
            CtorRateRejectionTestCase{"TargetRateNegative", kBroadcastRateHz, -1},
        };
    }
}  // namespace

class SoxrResamplerCtorRejectionTest : public ::testing::TestWithParam<CtorRateRejectionTestCase> {};

/**
 * @brief Every non-positive constructor rate raises std::invalid_argument.
 */
TEST_P(SoxrResamplerCtorRejectionTest, ThrowsInvalidArgument) {
    const auto& testCase{GetParam()};

    EXPECT_THROW(SoxrResampler(testCase.sourceRateHz, testCase.targetRateHz), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(NonPositiveParameter, SoxrResamplerCtorRejectionTest,
                         ::testing::ValuesIn(makeCtorRateRejectionTestCases()),
                         [](const ::testing::TestParamInfo<CtorRateRejectionTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct CtorRatePairTestCase {
        std::string_view name;
        int sourceRateHz;
        int targetRateHz;
    };

    /**
     * @brief Render a ctor rate-pair case as its name plus the rate pair for gtest listings
     *        and failure messages.
     */
    void PrintTo(const CtorRatePairTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {sourceRateHz=" << testCase.sourceRateHz << ", targetRateHz=" << testCase.targetRateHz
            << "}";
    }

    /**
     * @brief Representative rate-ratio shapes at the default Medium quality. Upsample and
     *        Downsample swap source/target around the canonical CD/Broadcast pair; Identity
     *        exercises the equal-rate configuration; ExtremeUpsample stresses a wide rate
     *        ratio that pushes libsoxr's filter dimensions toward their upper bound.
     */
    std::vector<CtorRatePairTestCase> makeCtorRatePairTestCases() {
        return {
            CtorRatePairTestCase{"Upsample", kCdAudioRateHz, kBroadcastRateHz},
            CtorRatePairTestCase{"Downsample", kBroadcastRateHz, kCdAudioRateHz},
            CtorRatePairTestCase{"Identity", kBroadcastRateHz, kBroadcastRateHz},
            CtorRatePairTestCase{"ExtremeUpsample", kNarrowRateHz, kVeryWideRateHz},
        };
    }
}  // namespace

class SoxrResamplerCtorRatePairTest : public ::testing::TestWithParam<CtorRatePairTestCase> {};

/**
 * @brief Every representative rate-ratio shape constructs without throwing at the default quality.
 */
TEST_P(SoxrResamplerCtorRatePairTest, ConstructsWithoutThrowing) {
    const auto& testCase{GetParam()};

    EXPECT_NO_THROW(SoxrResampler(testCase.sourceRateHz, testCase.targetRateHz));
}

INSTANTIATE_TEST_SUITE_P(RatioShapes, SoxrResamplerCtorRatePairTest, ::testing::ValuesIn(makeCtorRatePairTestCases()),
                         [](const ::testing::TestParamInfo<CtorRatePairTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct CtorQualityTestCase {
        std::string_view name;
        SoxrResampler::Quality quality;
    };

    /**
     * @brief Map a Quality preset to its enumerator name for human-readable failure messages.
     */
    [[nodiscard]] constexpr std::string_view qualityName(SoxrResampler::Quality quality) noexcept {
        switch (quality) {
            case SoxrResampler::Quality::Quick:
                return "Quick";
            case SoxrResampler::Quality::Low:
                return "Low";
            case SoxrResampler::Quality::Medium:
                return "Medium";
            case SoxrResampler::Quality::High:
                return "High";
            case SoxrResampler::Quality::VeryHigh:
                return "VeryHigh";
        }
        return "Unknown";
    }

    /**
     * @brief Render a ctor quality case as its name plus the resolved preset name for gtest
     *        listings and failure messages.
     */
    void PrintTo(const CtorQualityTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {quality=" << qualityName(testCase.quality) << "}";
    }

    /**
     * @brief Every quality preset other than Medium at the canonical upsample rate pair.
     *        Medium is already covered by the RatioShapes/Upsample case, so we skip it here
     *        to avoid the redundant pair.
     */
    std::vector<CtorQualityTestCase> makeCtorQualityTestCases() {
        return {
            CtorQualityTestCase{"Quick", SoxrResampler::Quality::Quick},
            CtorQualityTestCase{"Low", SoxrResampler::Quality::Low},
            CtorQualityTestCase{"High", SoxrResampler::Quality::High},
            CtorQualityTestCase{"VeryHigh", SoxrResampler::Quality::VeryHigh},
        };
    }
}  // namespace

class SoxrResamplerCtorQualityTest : public ::testing::TestWithParam<CtorQualityTestCase> {};

/**
 * @brief Every non-default quality preset constructs without throwing at the upsample rate pair.
 */
TEST_P(SoxrResamplerCtorQualityTest, ConstructsWithoutThrowing) {
    const auto& testCase{GetParam()};

    EXPECT_NO_THROW(SoxrResampler(kCdAudioRateHz, kBroadcastRateHz, testCase.quality));
}

INSTANTIATE_TEST_SUITE_P(AllPresets, SoxrResamplerCtorQualityTest, ::testing::ValuesIn(makeCtorQualityTestCases()),
                         [](const ::testing::TestParamInfo<CtorQualityTestCase>& info) {
                             return std::string{info.param.name};
                         });
