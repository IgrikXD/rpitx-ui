/**
 * @file test_audio_rate_converter.cpp
 * @brief Unit tests for the AudioRateConverter wrapper around SoxrResampler.
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
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "audio_rate_converter.h"
#include "signal_continuity.h"

namespace {
    /**
     * @brief CD-audio source rate (44.1 kHz). Paired with kBroadcastRateHz on the
     *        rate-converting path.
     */
    constexpr int kCdAudioRateHz{44'100};

    /**
     * @brief Broadcast target rate (48 kHz). Same value on both source and target
     *        trips the passthrough fast path.
     */
    constexpr int kBroadcastRateHz{48'000};

    /**
     * @brief Output block size used across the suite. 1024 is large enough for the
     *        rate-ratio math to produce meaningful Bresenham alternation and small
     *        enough to keep test input vectors trivially cheap to allocate.
     */
    constexpr std::size_t kDefaultOutputFrames{1024};

    /**
     * @brief Upper alternation value of peekNextInputFrames at the 44.1 -> 48 kHz pair
     *        with kDefaultOutputFrames. The 44100:48000 = 147:160 ratio gives an exact
     *        per-call average of 940.8 input frames; ceil(1024 * 44100 / 48000) = 941
     *        sets the ceiling that maxInputFrames() reports. Paired with
     *        kCdToBroadcastMinInputFrames.
     */
    constexpr std::size_t kCdToBroadcastMaxInputFrames{941};

    /**
     * @brief Lower alternation value of peekNextInputFrames at the same pair:
     *        floor(1024 * 44100 / 48000) = 940. Defined as Max - 1 so the floor/ceiling
     *        relationship stays explicit and immune to manual drift.
     */
    constexpr std::size_t kCdToBroadcastMinInputFrames{kCdToBroadcastMaxInputFrames - 1};
}  // namespace

/**
 * @brief Passthrough mode collapses every geometry getter to outputFrames.
 *
 * When sourceRateHz == targetRateHz the constructor skips soxr instantiation, so
 * peekNextInputFrames must match outputFrames on every call - each input block
 * maps 1:1 to the matching output block without any Bresenham bookkeeping.
 */
TEST(AudioRateConverterTest, PassthroughExposesSymmetricGeometry) {
    AudioRateConverter audioRateConverter{kBroadcastRateHz, kBroadcastRateHz, kDefaultOutputFrames};

    EXPECT_EQ(audioRateConverter.outputFrames(), kDefaultOutputFrames);
    EXPECT_EQ(audioRateConverter.maxInputFrames(), kDefaultOutputFrames);
    EXPECT_EQ(audioRateConverter.peekNextInputFrames(), kDefaultOutputFrames);
}

/**
 * @brief maxInputFrames matches the documented ceiling and bounds the initial peek.
 *
 * EXPECT_EQ checks the ceiling against kCdToBroadcastMaxInputFrames. The EXPECT_LE is
 * a state-zero sanity check that the initial peek fits the [0, maxInputFrames()] range;
 * the full Bresenham trajectory across many process() calls is covered by
 * BresenhamOscillatesBetweenTwoConsecutiveValues.
 */
TEST(AudioRateConverterTest, MaxInputBoundedByCeiledRatio) {
    AudioRateConverter audioRateConverter{kCdAudioRateHz, kBroadcastRateHz, kDefaultOutputFrames};

    EXPECT_EQ(audioRateConverter.maxInputFrames(), kCdToBroadcastMaxInputFrames);
    EXPECT_LE(audioRateConverter.peekNextInputFrames(), audioRateConverter.maxInputFrames());
}

/**
 * @brief Bresenham accumulator alternates per-call input between two consecutive integers.
 *
 * For 44100 -> 48000 with kDefaultOutputFrames the exact average input per call is 940.8,
 * so the Bresenham accumulator must alternate peekNextInputFrames between
 * kCdToBroadcastMinInputFrames (940) and kCdToBroadcastMaxInputFrames (941) - never a third
 * value - to keep the cumulative input/output ratio drift-free. 100 iterations both confirm
 * the two-value oscillation and assert that nothing else leaks through.
 */
TEST(AudioRateConverterTest, BresenhamOscillatesBetweenTwoConsecutiveValues) {
    AudioRateConverter audioRateConverter{kCdAudioRateHz, kBroadcastRateHz, kDefaultOutputFrames};
    std::vector<float> inputSamples;
    std::vector<float> outputSamples(audioRateConverter.outputFrames(), 0.0F);

    constexpr int kBresenhamIterations{100};
    int floorObservations{0};
    int ceilingObservations{0};
    for (int i{0}; i < kBresenhamIterations; ++i) {
        const std::size_t expectedInputFrames{audioRateConverter.peekNextInputFrames()};
        if (expectedInputFrames == kCdToBroadcastMinInputFrames) {
            ++floorObservations;
        } else if (expectedInputFrames == kCdToBroadcastMaxInputFrames) {
            ++ceilingObservations;
        }
        inputSamples.assign(expectedInputFrames, 0.0F);
        ASSERT_NE(audioRateConverter.process(inputSamples, outputSamples), AudioRateConverterStatus::Error);
    }

    EXPECT_GT(floorObservations, 0);
    EXPECT_GT(ceilingObservations, 0);
    EXPECT_EQ(floorObservations + ceilingObservations, kBresenhamIterations);
}

/**
 * @brief Passthrough process() copies the input span element-wise into output.
 *
 * Distinguishable per-sample values (1..8) verify that no arithmetic, scaling, or
 * silence padding has been applied - input and output buffers must compare equal
 * after the call. An all-zero input would still pass a no-op buggy implementation,
 * so a non-trivial pattern is required to catch a fill-with-zero regression.
 */
TEST(AudioRateConverterTest, PassthroughLeavesSamplesIntact) {
    constexpr std::size_t kDistinctSampleCount{8};
    AudioRateConverter audioRateConverter{kBroadcastRateHz, kBroadcastRateHz, kDistinctSampleCount};
    std::vector<float> inputSamples{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F};
    std::vector<float> outputSamples(kDistinctSampleCount, 0.0F);

    ASSERT_EQ(audioRateConverter.process(inputSamples, outputSamples), AudioRateConverterStatus::Ok);

    EXPECT_EQ(inputSamples, outputSamples);
}

/**
 * @brief process() accepts the input when the buffers match the converter's geometry.
 *
 * Whether the very first block already completes an output block depends on how much
 * libsoxr holds back, so both Ok and NeedsInput are valid here - only Error would mean
 * that a correctly sized block was rejected.
 */
TEST(AudioRateConverterTest, ProcessSucceedsOnRealRateConversion) {
    AudioRateConverter audioRateConverter{kCdAudioRateHz, kBroadcastRateHz, kDefaultOutputFrames};
    std::vector<float> inputSamples(audioRateConverter.peekNextInputFrames(), 0.0F);
    std::vector<float> outputSamples(audioRateConverter.outputFrames(), 0.0F);

    EXPECT_NE(audioRateConverter.process(inputSamples, outputSamples), AudioRateConverterStatus::Error);
}

/**
 * @brief Passthrough drain() reports success and emits zero frames.
 *
 * Without a soxr instance there is no filter tail and no queue to flush, so drain() returns
 * a present optional holding 0. nullopt would signal a hard pipeline failure which the
 * passthrough path cannot produce.
 */
TEST(AudioRateConverterTest, PassthroughDrainProducesZeroFrames) {
    AudioRateConverter audioRateConverter{kBroadcastRateHz, kBroadcastRateHz, kDefaultOutputFrames};
    std::vector<float> outputSamples(audioRateConverter.outputFrames(), 0.0F);

    const auto produced{audioRateConverter.drain(outputSamples)};

    ASSERT_TRUE(produced.has_value());
    EXPECT_EQ(produced.value(), 0U);
}

/**
 * @brief drain() returns a present optional after real-rate processing.
 *
 * Ten blocks of constant DC populate soxr's filter delay line. The exact tail length the
 * converter then emits depends on internal libsoxr state and is not a stable contract; the
 * stable contracts are that drain() succeeds (non-nullopt) and that the reported frame
 * count never exceeds the caller's buffer.
 */
TEST(AudioRateConverterTest, DrainSucceedsAfterRealConversion) {
    AudioRateConverter audioRateConverter{kCdAudioRateHz, kBroadcastRateHz, kDefaultOutputFrames};
    std::vector<float> inputSamples;
    std::vector<float> outputSamples(audioRateConverter.outputFrames(), 0.0F);

    constexpr int kDelayLinePrimingBlocks{10};
    for (int i{0}; i < kDelayLinePrimingBlocks; ++i) {
        inputSamples.assign(audioRateConverter.peekNextInputFrames(), 1.0F);
        ASSERT_NE(audioRateConverter.process(inputSamples, outputSamples), AudioRateConverterStatus::Error);
    }

    const auto produced{audioRateConverter.drain(outputSamples)};

    ASSERT_TRUE(produced.has_value());
    EXPECT_LE(produced.value(), outputSamples.size());
}

namespace {
    struct AudioRateConverterCtorRejectionTestCase {
        std::string_view name;
        int sourceRateHz;
        int targetRateHz;
        std::size_t targetOutputFrames;
    };

    /**
     * @brief Render a ctor-rejection case as its name plus the offending parameter triple
     *        for gtest listings and failure messages.
     */
    void PrintTo(const AudioRateConverterCtorRejectionTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {sourceRateHz=" << testCase.sourceRateHz << ", targetRateHz=" << testCase.targetRateHz
            << ", targetOutputFrames=" << testCase.targetOutputFrames << "}";
    }

    /**
     * @brief Constructor parameter triples that violate the (sourceRateHz > 0,
     *        targetRateHz > 0, targetOutputFrames > 0) precondition. One named case per
     *        distinct violation channel; size_t cannot be negative so targetOutputFrames
     *        has only the zero variant.
     */
    std::vector<AudioRateConverterCtorRejectionTestCase> makeAudioRateConverterCtorRejectionTestCases() {
        return {
            AudioRateConverterCtorRejectionTestCase{"SourceRateZero", 0, kBroadcastRateHz, kDefaultOutputFrames},
            AudioRateConverterCtorRejectionTestCase{"SourceRateNegative", -1, kBroadcastRateHz, kDefaultOutputFrames},
            AudioRateConverterCtorRejectionTestCase{"TargetRateZero", kBroadcastRateHz, 0, kDefaultOutputFrames},
            AudioRateConverterCtorRejectionTestCase{"TargetRateNegative", kBroadcastRateHz, -1, kDefaultOutputFrames},
            AudioRateConverterCtorRejectionTestCase{"OutputFramesZero", kBroadcastRateHz, kBroadcastRateHz, 0},
        };
    }
}  // namespace

class AudioRateConverterCtorRejectionTest : public ::testing::TestWithParam<AudioRateConverterCtorRejectionTestCase> {};

/**
 * @brief Every non-positive constructor parameter raises std::invalid_argument.
 */
TEST_P(AudioRateConverterCtorRejectionTest, ThrowsInvalidArgument) {
    const auto& testCase{GetParam()};

    EXPECT_THROW(AudioRateConverter(testCase.sourceRateHz, testCase.targetRateHz, testCase.targetOutputFrames),
                 std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(NonPositiveParameter, AudioRateConverterCtorRejectionTest,
                         ::testing::ValuesIn(makeAudioRateConverterCtorRejectionTestCases()),
                         [](const ::testing::TestParamInfo<AudioRateConverterCtorRejectionTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct AudioRateConverterProcessRejectionTestCase {
        std::string_view name;
        std::size_t inputSize;
        std::size_t outputSize;
    };

    /**
     * @brief Render a process-rejection case as its name and the two span sizes for gtest
     *        listings and failure messages.
     */
    void PrintTo(const AudioRateConverterProcessRejectionTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {inputSize=" << testCase.inputSize << ", outputSize=" << testCase.outputSize << "}";
    }

    /**
     * @brief Buffer geometries that violate the process() contract on the passthrough path
     *        (input.size() must equal peekNextInputFrames(); output.size() must equal
     *        outputFrames()). In passthrough peekNextInputFrames() returns
     *        kDefaultOutputFrames, so 1024/1024 is the only valid pair and either sibling
     *        shrunk to half triggers rejection.
     */
    std::vector<AudioRateConverterProcessRejectionTestCase> makeAudioRateConverterProcessRejectionTestCases() {
        return {
            AudioRateConverterProcessRejectionTestCase{
                "WrongInputSize", kDefaultOutputFrames / 2, kDefaultOutputFrames},
            AudioRateConverterProcessRejectionTestCase{
                "WrongOutputSize", kDefaultOutputFrames, kDefaultOutputFrames / 2},
        };
    }
}  // namespace

class AudioRateConverterProcessRejectionTest
    : public ::testing::TestWithParam<AudioRateConverterProcessRejectionTestCase> {};

/**
 * @brief process() reports failure when either span violates the geometry contract.
 *
 * Mismatched span sizes return Error instead of corrupting the staging buffer or producing
 * partial output - the caller treats Error as a hard contract violation and propagates it
 * up the pipeline rather than swallowing the discrepancy.
 */
TEST_P(AudioRateConverterProcessRejectionTest, ReportsFailure) {
    const auto& testCase{GetParam()};
    AudioRateConverter audioRateConverter{kBroadcastRateHz, kBroadcastRateHz, kDefaultOutputFrames};
    std::vector<float> inputSamples(testCase.inputSize, 0.0F);
    std::vector<float> outputSamples(testCase.outputSize, 0.0F);

    EXPECT_EQ(audioRateConverter.process(inputSamples, outputSamples), AudioRateConverterStatus::Error);
}

INSTANTIATE_TEST_SUITE_P(OffendingSpan, AudioRateConverterProcessRejectionTest,
                         ::testing::ValuesIn(makeAudioRateConverterProcessRejectionTestCases()),
                         [](const ::testing::TestParamInfo<AudioRateConverterProcessRejectionTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct AudioRateConverterContinuityTestCase {
        std::string_view name;
        int sourceRateHz;
        int targetRateHz;
        std::size_t outputFrames;
    };

    /**
     * @brief Render a continuity case as its name plus the rate pair and the output block size
     *        for gtest listings and failure messages.
     */
    void PrintTo(const AudioRateConverterContinuityTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {" << testCase.sourceRateHz << " -> " << testCase.targetRateHz
            << " Hz, outputFrames=" << testCase.outputFrames << "}";
    }

    /**
     * @brief Rate pairs and output block sizes of the audio transmitters: CD and 8 kHz audio
     *        into the 48 kHz rate of pinfm / piam / pissb (1024-frame blocks), CD and 48 kHz
     *        audio into the 228 kHz MPX rate of pifmrds (4096-frame blocks).
     */
    std::vector<AudioRateConverterContinuityTestCase> makeAudioRateConverterContinuityTestCases() {
        constexpr int kVoiceRateHz{8'000};
        constexpr int kMpxRateHz{228'000};
        constexpr std::size_t kMpxOutputFrames{4096};
        return {
            AudioRateConverterContinuityTestCase{
                "CdToBroadcast", kCdAudioRateHz, kBroadcastRateHz, kDefaultOutputFrames},
            AudioRateConverterContinuityTestCase{
                "VoiceToBroadcast", kVoiceRateHz, kBroadcastRateHz, kDefaultOutputFrames},
            AudioRateConverterContinuityTestCase{"CdToMpx", kCdAudioRateHz, kMpxRateHz, kMpxOutputFrames},
            AudioRateConverterContinuityTestCase{"BroadcastToMpx", kBroadcastRateHz, kMpxRateHz, kMpxOutputFrames},
        };
    }
}  // namespace

class AudioRateConverterContinuityTest : public ::testing::TestWithParam<AudioRateConverterContinuityTestCase> {};

/**
 * @brief The converted signal carries no silence between its first and its last sample.
 *
 * The input is one second of a tone on a DC offset that never comes near zero, so a zero inside
 * the output can only be padding. libsoxr hands its output over in bursts - some input blocks
 * yield nothing, others more than an output block - and a converter that pads the short blocks
 * puts silence into the audio within the first tenths of a second for every pair of the matrix.
 * The leading assertion rules out a converter that never completes a block.
 */
TEST_P(AudioRateConverterContinuityTest, EmitsNoSilenceInsideSignal) {
    const auto& testCase{GetParam()};
    AudioRateConverter audioRateConverter{testCase.sourceRateHz, testCase.targetRateHz, testCase.outputFrames};
    const std::vector<float> sourceSignal{
        makeOffsetTone(testCase.sourceRateHz, static_cast<std::size_t>(testCase.sourceRateHz))};
    std::vector<float> outputSamples(audioRateConverter.outputFrames(), 0.0F);
    std::vector<float> convertedSignal;

    std::size_t consumedFrames{0};
    while (consumedFrames + audioRateConverter.peekNextInputFrames() <= sourceSignal.size()) {
        const std::span<const float> inputSamples{sourceSignal.data() + consumedFrames,
                                                  audioRateConverter.peekNextInputFrames()};
        consumedFrames += inputSamples.size();
        const AudioRateConverterStatus status{audioRateConverter.process(inputSamples, outputSamples)};
        ASSERT_NE(status, AudioRateConverterStatus::Error);
        if (status == AudioRateConverterStatus::Ok) {
            convertedSignal.insert(convertedSignal.end(), outputSamples.begin(), outputSamples.end());
        }
    }

    ASSERT_FALSE(convertedSignal.empty());
    EXPECT_EQ(countSilenceInsideSignal(convertedSignal), 0U);
}

/**
 * @brief Output blocks and drained tail together return every input sample once and in order.
 *
 * The frames of all Ok blocks plus the frames drain() returns have to add up to
 * consumedFrames * targetRate / sourceRate within one frame of rounding, and sample i of the
 * output has to be sample i of the tone at the target rate: libsoxr compensates its own delay,
 * so a dropped, repeated or reordered sample shifts the tone's phase from that point on, which
 * the 1e-3 tolerance exposes (one sample at 228 kHz already moves the tone by 7e-3). The first
 * and the last 50 ms are left out, where the transients of the start and of the end dominate.
 * A converter that pads a block, hands queued samples out twice, drops them when it compacts
 * its queue or queues a burst in front of the older samples fails one of the two checks.
 */
TEST_P(AudioRateConverterContinuityTest, ReturnsEverySampleOnceAndInOrder) {
    constexpr float kToneTolerance{1e-3F};
    constexpr double kSkipSeconds{0.05};
    const auto& testCase{GetParam()};
    AudioRateConverter audioRateConverter{testCase.sourceRateHz, testCase.targetRateHz, testCase.outputFrames};
    const std::vector<float> sourceSignal{
        makeOffsetTone(testCase.sourceRateHz, static_cast<std::size_t>(testCase.sourceRateHz))};
    std::vector<float> outputSamples(audioRateConverter.outputFrames(), 0.0F);
    std::vector<float> convertedSignal;

    std::size_t consumedFrames{0};
    while (consumedFrames + audioRateConverter.peekNextInputFrames() <= sourceSignal.size()) {
        const std::span<const float> inputSamples{sourceSignal.data() + consumedFrames,
                                                  audioRateConverter.peekNextInputFrames()};
        consumedFrames += inputSamples.size();
        const AudioRateConverterStatus status{audioRateConverter.process(inputSamples, outputSamples)};
        ASSERT_NE(status, AudioRateConverterStatus::Error);
        if (status == AudioRateConverterStatus::Ok) {
            convertedSignal.insert(convertedSignal.end(), outputSamples.begin(), outputSamples.end());
        }
    }
    while (true) {
        const auto drainedFrames{audioRateConverter.drain(outputSamples)};
        ASSERT_TRUE(drainedFrames.has_value());
        if (drainedFrames.value() == 0) {
            break;
        }
        convertedSignal.insert(
            convertedSignal.end(), outputSamples.begin(), outputSamples.begin() + drainedFrames.value());
    }

    const double expectedFrames{static_cast<double>(consumedFrames) * testCase.targetRateHz / testCase.sourceRateHz};
    const auto skipFrames{static_cast<std::size_t>(kSkipSeconds * testCase.targetRateHz)};
    EXPECT_NEAR(static_cast<double>(convertedSignal.size()), expectedFrames, 1.0);
    EXPECT_EQ(countSamplesOffTone(convertedSignal, testCase.targetRateHz, skipFrames, kToneTolerance), 0U);
}

INSTANTIATE_TEST_SUITE_P(TransmitterRatePairs, AudioRateConverterContinuityTest,
                         ::testing::ValuesIn(makeAudioRateConverterContinuityTestCases()),
                         [](const ::testing::TestParamInfo<AudioRateConverterContinuityTestCase>& info) {
                             return std::string{info.param.name};
                         });
