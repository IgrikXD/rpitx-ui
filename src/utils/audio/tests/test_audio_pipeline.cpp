/**
 * @file test_audio_pipeline.cpp
 * @brief Unit tests for validateAudioFormat, validateLoopSupport, and AudioPipeline.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "audio_pipeline.h"
#include "captured_streams_mixin.h"
#include "fake_audio_source.h"

namespace {
    /**
     * @brief Inclusive lower bound on the audio sample rate accepted by validateAudioFormat -
     *        matches the lower end of the (8 kHz - 192 kHz) window declared by the project's
     *        CLI front-ends.
     */
    constexpr int kMinSampleRateHz{8'000};

    /**
     * @brief Inclusive upper bound on the audio sample rate accepted by validateAudioFormat -
     *        matches the upper end of the (8 kHz - 192 kHz) window declared by the project's
     *        CLI front-ends.
     */
    constexpr int kMaxSampleRateHz{192'000};

    /**
     * @brief Broadcast sample rate (48 kHz). Default for every non-resample pipeline test
     *        and for the mono validate case.
     */
    constexpr int kBroadcastRateHz{48'000};

    /**
     * @brief CD-audio sample rate (44.1 kHz). Paired with stereo on the stereo validate case
     *        and used as the asymmetric peer of kBroadcastRateHz in the resample sweep.
     */
    constexpr int kCdAudioRateHz{44'100};
}  // namespace

namespace {
    struct ValidateAudioFormatAcceptTestCase {
        std::string_view name;
        int channels;
        int sampleRate;
    };

    /**
     * @brief Render a ValidateAudioFormatAcceptTestCase as its name plus the (channels,
     *        sampleRate) pair for gtest listings and failure messages.
     */
    void PrintTo(const ValidateAudioFormatAcceptTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {channels=" << testCase.channels << ", sampleRate=" << testCase.sampleRate << "}";
    }

    /**
     * @brief Four accept cases covering both supported channel layouts and both inclusive
     *        sample-rate boundaries: mono / stereo mid-range, mono at min boundary, mono at
     *        max boundary. A regression that flipped the channel branch or shifted a boundary
     *        by one Hz trips exactly the affected case.
     */
    std::vector<ValidateAudioFormatAcceptTestCase> makeValidateAudioFormatAcceptTestCases() {
        return {
            ValidateAudioFormatAcceptTestCase{"MonoBroadcast", 1, kBroadcastRateHz},
            ValidateAudioFormatAcceptTestCase{"StereoCdAudio", 2, kCdAudioRateHz},
            ValidateAudioFormatAcceptTestCase{"MonoAtMinBoundary", 1, kMinSampleRateHz},
            ValidateAudioFormatAcceptTestCase{"MonoAtMaxBoundary", 1, kMaxSampleRateHz},
        };
    }
}  // namespace

class ValidateAudioFormatAcceptTest
    : public CapturedStreamsMixin<::testing::TestWithParam<ValidateAudioFormatAcceptTestCase>> {};

/**
 * @brief Accepted format returns true and emits no stderr diagnostic.
 *
 * The stderr-empty check pins down the silent-success half of the contract: a regression
 * that returned true while still emitting a warning would slip past a boolean-only check.
 */
TEST_P(ValidateAudioFormatAcceptTest, AcceptsAndStaysSilent) {
    const auto& testCase{GetParam()};

    const bool accepted{validateAudioFormat(
        AudioFormat{
            .channels   = testCase.channels,
            .sampleRate = testCase.sampleRate,
        },
        kMinSampleRateHz,
        kMaxSampleRateHz)};

    EXPECT_TRUE(accepted);
    EXPECT_TRUE(capturedStderr().empty());
}

INSTANTIATE_TEST_SUITE_P(InRange, ValidateAudioFormatAcceptTest,
                         ::testing::ValuesIn(makeValidateAudioFormatAcceptTestCases()),
                         [](const ::testing::TestParamInfo<ValidateAudioFormatAcceptTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct ValidateAudioFormatRejectTestCase {
        std::string_view name;
        int channels;
        int sampleRate;
        std::string_view diagnosticSubstring;
    };

    /**
     * @brief Render a ValidateAudioFormatRejectTestCase as its name plus the (channels,
     *        sampleRate) pair for gtest listings and failure messages.
     */
    void PrintTo(const ValidateAudioFormatRejectTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {channels=" << testCase.channels << ", sampleRate=" << testCase.sampleRate << "}";
    }

    /**
     * @brief Two cases per rejection branch in the SUT: the channel branch with zero and five
     *        channels (covering "below 1" and "above 2"), the range branch with one Hz outside
     *        each inclusive boundary. diagnosticSubstring pins the stderr text to its branch,
     *        catching a regression where the wrong branch's message fires.
     */
    std::vector<ValidateAudioFormatRejectTestCase> makeValidateAudioFormatRejectTestCases() {
        return {
            ValidateAudioFormatRejectTestCase{"ZeroChannels", 0, kBroadcastRateHz, "mono or stereo"},
            ValidateAudioFormatRejectTestCase{"FiveChannels", 5, kBroadcastRateHz, "mono or stereo"},
            ValidateAudioFormatRejectTestCase{
                "RateBelowMinimum", 1, kMinSampleRateHz - 1, "outside the supported range"},
            ValidateAudioFormatRejectTestCase{
                "RateAboveMaximum", 1, kMaxSampleRateHz + 1, "outside the supported range"},
        };
    }
}  // namespace

class ValidateAudioFormatRejectTest
    : public CapturedStreamsMixin<::testing::TestWithParam<ValidateAudioFormatRejectTestCase>> {};

/**
 * @brief Rejected format returns false and emits the branch-specific stderr diagnostic.
 *
 * The substring check pins the diagnostic to its rejection branch instead of accepting any
 * non-empty stderr; together with the boolean-return assertion this catches both a wrong-branch
 * regression and a silent rejection that forgot the diagnostic entirely.
 */
TEST_P(ValidateAudioFormatRejectTest, RejectsAndEmitsBranchSpecificDiagnostic) {
    const auto& testCase{GetParam()};

    const bool accepted{validateAudioFormat(
        AudioFormat{
            .channels   = testCase.channels,
            .sampleRate = testCase.sampleRate,
        },
        kMinSampleRateHz,
        kMaxSampleRateHz)};

    EXPECT_FALSE(accepted);
    EXPECT_NE(capturedStderr().find(testCase.diagnosticSubstring), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(OutOfRange, ValidateAudioFormatRejectTest,
                         ::testing::ValuesIn(makeValidateAudioFormatRejectTestCases()),
                         [](const ::testing::TestParamInfo<ValidateAudioFormatRejectTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct ValidateLoopSupportTestCase {
        std::string_view name;
        bool seekable;
        bool loopRequested;
        bool expectedAccepted;
        std::optional<std::string_view> diagnosticSubstring;  ///< std::nullopt on accept cases.
    };

    /**
     * @brief Render a ValidateLoopSupportTestCase as its name plus the (seekable, loopRequested)
     *        pair for gtest listings and failure messages. std::boolalpha keeps the booleans
     *        readable in failure output ("seekable=false" rather than "seekable=0").
     */
    void PrintTo(const ValidateLoopSupportTestCase& testCase, std::ostream* os) {
        *os << std::boolalpha << testCase.name << " {seekable=" << testCase.seekable
            << ", loopRequested=" << testCase.loopRequested << "}";
    }

    /**
     * @brief Full 2x2 truth table over (seekable x loopRequested). The loop-not-requested
     *        branch short-circuits to accept regardless of seekable; the loop-requested branch
     *        accepts only when seekable. The single rejection corner (loopRequested=true,
     *        seekable=false) emits a "not seekable" diagnostic. The std::nullopt diagnostic on
     *        the three accept cases drives the per-case stderr-empty assertion in the body.
     */
    std::vector<ValidateLoopSupportTestCase> makeValidateLoopSupportTestCases() {
        return {
            ValidateLoopSupportTestCase{"NoLoopOnNonSeekable", false, false, true, std::nullopt},
            ValidateLoopSupportTestCase{"NoLoopOnSeekable", true, false, true, std::nullopt},
            ValidateLoopSupportTestCase{"LoopOnSeekable", true, true, true, std::nullopt},
            ValidateLoopSupportTestCase{"LoopOnNonSeekable", false, true, false, "not seekable"},
        };
    }
}  // namespace

class ValidateLoopSupportTest : public CapturedStreamsMixin<::testing::TestWithParam<ValidateLoopSupportTestCase>> {};

/**
 * @brief Decision and diagnostic match the per-case truth-table outcome.
 *
 * Sweep over the four (seekable, loopRequested) combinations: three accept silently and the
 * remaining (loopRequested=true, seekable=false) corner rejects with a "not seekable"
 * diagnostic. The accept/reject shape of the stderr check is keyed by whether the case
 * carries an expected diagnosticSubstring, so each case owns its full expected outcome.
 */
TEST_P(ValidateLoopSupportTest, MatchesExpectedOutcome) {
    const auto& testCase{GetParam()};
    FakeAudioSource source{AudioFormat{
                               .channels   = 1,
                               .sampleRate = kBroadcastRateHz,
                           },
                           testCase.seekable};

    const bool accepted{validateLoopSupport(source, testCase.loopRequested)};

    EXPECT_EQ(accepted, testCase.expectedAccepted);
    if (testCase.diagnosticSubstring.has_value()) {
        EXPECT_NE(capturedStderr().find(testCase.diagnosticSubstring.value()), std::string::npos);
    } else {
        EXPECT_TRUE(capturedStderr().empty());
    }
}

INSTANTIATE_TEST_SUITE_P(TruthTable, ValidateLoopSupportTest, ::testing::ValuesIn(makeValidateLoopSupportTestCases()),
                         [](const ::testing::TestParamInfo<ValidateLoopSupportTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct AudioPipelineCtorRejectionTestCase {
        std::string_view name;
        int sourceChannels;
        std::size_t targetOutputFrames;
    };

    /**
     * @brief Render an AudioPipelineCtorRejectionTestCase as its name plus the (sourceChannels,
     *        targetOutputFrames) pair for gtest listings and failure messages.
     */
    void PrintTo(const AudioPipelineCtorRejectionTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {sourceChannels=" << testCase.sourceChannels
            << ", targetOutputFrames=" << testCase.targetOutputFrames << "}";
    }

    /**
     * @brief One case per rejection branch in the ctor: zero source channels is rejected by
     *        the AudioPipeline ctor's own channel-count check before any rate-converter
     *        construction; zero target output frames propagates std::invalid_argument from
     *        the per-channel AudioRateConverter the pipeline constructs internally. Surfacing
     *        the same exception type from both branches keeps the rejection contract uniform
     *        for the caller.
     */
    std::vector<AudioPipelineCtorRejectionTestCase> makeAudioPipelineCtorRejectionTestCases() {
        return {
            AudioPipelineCtorRejectionTestCase{"ZeroChannels", 0, 1024},
            AudioPipelineCtorRejectionTestCase{"ZeroTargetOutputFrames", 1, 0},
        };
    }
}  // namespace

class AudioPipelineCtorTest
    : public CapturedStreamsMixin<::testing::TestWithParam<AudioPipelineCtorRejectionTestCase>> {};

/**
 * @brief Ctor throws std::invalid_argument for any rejected config and emits no stderr diagnostic.
 *
 * The exception type is uniform across the two rejection branches (channel-count check in
 * AudioPipeline::AudioPipeline; targetOutputFrames check in the per-channel
 * AudioRateConverter ctor), so the test does not need to differentiate which branch fired -
 * surfacing std::invalid_argument from any rejected config is the public contract. The
 * stderr-empty check pins the silence half: a future ctor that printed a diagnostic before
 * throwing would slip past a throw-only assertion.
 */
TEST_P(AudioPipelineCtorTest, ThrowsInvalidArgument) {
    const auto& testCase{GetParam()};
    FakeAudioSource source{AudioFormat{
                               .channels   = testCase.sourceChannels,
                               .sampleRate = kBroadcastRateHz,
                           },
                           true};

    EXPECT_THROW(AudioPipeline(source,
                               AudioPipelineConfig{
                                   .loop               = false,
                                   .targetSampleRate   = kBroadcastRateHz,
                                   .targetOutputFrames = testCase.targetOutputFrames,
                                   .channelMode        = AudioChannelMode::Mono,
                               }),
                 std::invalid_argument);
    EXPECT_TRUE(capturedStderr().empty());
}

INSTANTIATE_TEST_SUITE_P(RejectedConfig, AudioPipelineCtorTest,
                         ::testing::ValuesIn(makeAudioPipelineCtorRejectionTestCases()),
                         [](const ::testing::TestParamInfo<AudioPipelineCtorRejectionTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct OutputGeometryTestCase {
        std::string_view name;
        int sourceChannels;
        AudioChannelMode channelMode;
        std::size_t targetOutputFrames;
        int expectedOutputChannels;
        std::size_t expectedOutputSamplesPerBlock;
    };

    /**
     * @brief Render an OutputGeometryTestCase as its name plus the (sourceChannels, channelMode,
     *        targetOutputFrames) triple for gtest listings.
     */
    void PrintTo(const OutputGeometryTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {sourceChannels=" << testCase.sourceChannels
            << ", channelMode=" << (testCase.channelMode == AudioChannelMode::Mono ? "Mono" : "Preserve")
            << ", targetOutputFrames=" << testCase.targetOutputFrames << "}";
    }

    /**
     * @brief Three cases covering the distinct output-geometry shapes the pipeline can produce:
     *        downmix path (stereo source through Mono mode collapses to one channel), multichannel
     *        passthrough (stereo source through Preserve mode keeps both channels), single-channel
     *        passthrough (mono source through Preserve mode is trivial). The fourth (mono source,
     *        Mono mode) combination is omitted because it yields the same output geometry as
     *        (mono source, Preserve mode) and would not exercise a distinct branch.
     *        expectedOutputSamplesPerBlock is outputChannels * targetOutputFrames - listing the
     *        derived value explicitly lets the test catch a regression in either factor or in
     *        the multiplication.
     */
    std::vector<OutputGeometryTestCase> makeOutputGeometryTestCases() {
        return {
            OutputGeometryTestCase{"StereoSourceMono", 2, AudioChannelMode::Mono, 512, 1, 512},
            OutputGeometryTestCase{"StereoSourcePreserve", 2, AudioChannelMode::Preserve, 256, 2, 512},
            OutputGeometryTestCase{"MonoSourcePreserve", 1, AudioChannelMode::Preserve, 256, 1, 256},
        };
    }
}  // namespace

class AudioPipelineOutputGeometryTest : public CapturedStreamsMixin<::testing::TestWithParam<OutputGeometryTestCase>> {
};

/**
 * @brief Output dimensions follow channelMode and targetOutputFrames, and construction stays silent on stderr.
 *
 * Mono mode collapses any source-channel count to 1; Preserve mode passes the source-channel
 * count through. outputSamplesPerBlock() multiplies frames by the resulting channel count -
 * checking that derived value alongside the two primary getters catches a regression in
 * either factor or the multiplication itself. The stderr-empty check pins the
 * construction-path silence: a ctor or getter that leaked a warning would slip past the
 * value checks.
 */
TEST_P(AudioPipelineOutputGeometryTest, ReportsExpectedOutputDimensions) {
    const auto& testCase{GetParam()};
    FakeAudioSource source{AudioFormat{
                               .channels   = testCase.sourceChannels,
                               .sampleRate = kBroadcastRateHz,
                           },
                           true};
    AudioPipeline pipeline(source,
                           AudioPipelineConfig{
                               .loop               = false,
                               .targetSampleRate   = kBroadcastRateHz,
                               .targetOutputFrames = testCase.targetOutputFrames,
                               .channelMode        = testCase.channelMode,
                           });

    EXPECT_EQ(pipeline.outputChannels(), testCase.expectedOutputChannels);
    EXPECT_EQ(pipeline.outputFrames(), testCase.targetOutputFrames);
    EXPECT_EQ(pipeline.outputSamplesPerBlock(), testCase.expectedOutputSamplesPerBlock);
    EXPECT_TRUE(capturedStderr().empty());
}

INSTANTIATE_TEST_SUITE_P(ChannelModeMatrix, AudioPipelineOutputGeometryTest,
                         ::testing::ValuesIn(makeOutputGeometryTestCases()),
                         [](const ::testing::TestParamInfo<OutputGeometryTestCase>& info) {
                             return std::string{info.param.name};
                         });

/**
 * @brief read() throws std::invalid_argument when the destination size is not outputSamplesPerBlock().
 *
 * The output buffer must exactly match the geometry declared by the ctor. The two EXPECT_THROW
 * calls probe the `!=` check from both sides - one sample undersize and one sample oversize -
 * which catches an off-by-one regression that a single far-from-boundary check (e.g. size=64
 * against expected 1024) would let slip through.
 */
TEST(AudioPipelineReadTest, ThrowsOnMismatchedOutputBlockSize) {
    FakeAudioSource source{AudioFormat{
                               .channels   = 1,
                               .sampleRate = kBroadcastRateHz,
                           },
                           true};
    AudioPipeline pipeline(source,
                           AudioPipelineConfig{
                               .loop               = false,
                               .targetSampleRate   = kBroadcastRateHz,
                               .targetOutputFrames = 1024,
                               .channelMode        = AudioChannelMode::Mono,
                           });
    std::vector<float> undersizedOutputBlock(pipeline.outputSamplesPerBlock() - 1, 0.0F);
    std::vector<float> oversizedOutputBlock(pipeline.outputSamplesPerBlock() + 1, 0.0F);

    EXPECT_THROW(
        { [[maybe_unused]] const AudioPipelineStatus status{pipeline.read(undersizedOutputBlock)}; },
        std::invalid_argument);
    EXPECT_THROW(
        { [[maybe_unused]] const AudioPipelineStatus status{pipeline.read(oversizedOutputBlock)}; },
        std::invalid_argument);
}

/**
 * @brief Mono passthrough reproduces the input sample-by-sample bit-exactly.
 *
 * Equal source/target rate with a mono source collapses the data path to four std::copy
 * hops (libsoxr is not even instantiated) - no arithmetic touches the sample values, so
 * gtest's 4-ULP float precision is the right strictness; any larger drift signals a
 * non-passthrough code path leaking into the equal-rate mono case. kSourceFrames is a
 * multiple of targetOutputFrames so the inner loop indexes referenceSamples in-bounds
 * through every Ok block.
 */
TEST(AudioPipelineReadTest, MonoPassthroughReproducesInput) {
    constexpr std::size_t kSourceFrames{4096};
    std::vector<float> samples(kSourceFrames);
    for (std::size_t frame{0}; frame < kSourceFrames; ++frame) {
        samples[frame] = static_cast<float>(frame) / static_cast<float>(kSourceFrames) * 0.5F;
    }
    const auto referenceSamples{samples};
    FakeAudioSource source{AudioFormat{
                               .channels   = 1,
                               .sampleRate = kBroadcastRateHz,
                           },
                           std::move(samples),
                           true};
    AudioPipeline pipeline(source,
                           AudioPipelineConfig{
                               .loop               = false,
                               .targetSampleRate   = kBroadcastRateHz,
                               .targetOutputFrames = 1024,
                               .channelMode        = AudioChannelMode::Mono,
                           });

    std::vector<float> outputBlock(pipeline.outputSamplesPerBlock(), 99.0F);
    std::size_t comparedSampleCount{0};
    while (true) {
        const auto status{pipeline.read(outputBlock)};
        if (status == AudioPipelineStatus::End) {
            break;
        }
        ASSERT_EQ(status, AudioPipelineStatus::Ok);
        for (std::size_t sampleIdx{0}; sampleIdx < outputBlock.size(); ++sampleIdx, ++comparedSampleCount) {
            EXPECT_FLOAT_EQ(outputBlock[sampleIdx], referenceSamples[comparedSampleCount])
                << "Mismatch at sample " << comparedSampleCount;
        }
    }
    EXPECT_EQ(comparedSampleCount, referenceSamples.size());
}

/**
 * @brief Stereo source through Mono mode averages L and R to the arithmetic mean of the channel levels.
 *
 * Every source frame is filled with (L=1.0, R=0.5), so a correct arithmetic-mean downmix
 * produces 0.75 for every output sample. The asymmetric levels are deliberate: a regression
 * that returned only the first channel (1.0) or only the second (0.5) trips the EXPECT_NEAR,
 * and so does a sum-without-scaling bug (1.5) or a sign-flip on the average (-0.75). A
 * symmetric (L=+1, R=-1) fixture would average to zero and let sum-without-scaling and
 * sign-flip regressions slip through silently.
 */
TEST(AudioPipelineReadTest, StereoMonoModeAveragesChannels) {
    constexpr std::size_t kSourceFrames{2048};
    std::vector<float> samples(kSourceFrames * 2);
    for (std::size_t frame{0}; frame < kSourceFrames; ++frame) {
        samples[frame * 2 + 0] = 1.0F;
        samples[frame * 2 + 1] = 0.5F;
    }
    FakeAudioSource source{AudioFormat{
                               .channels   = 2,
                               .sampleRate = kBroadcastRateHz,
                           },
                           std::move(samples),
                           true};
    AudioPipeline pipeline(source,
                           AudioPipelineConfig{
                               .loop               = false,
                               .targetSampleRate   = kBroadcastRateHz,
                               .targetOutputFrames = 1024,
                               .channelMode        = AudioChannelMode::Mono,
                           });

    std::vector<float> outputBlock(pipeline.outputSamplesPerBlock(), 99.0F);
    const auto status{pipeline.read(outputBlock)};

    ASSERT_EQ(status, AudioPipelineStatus::Ok);
    for (float sample: outputBlock) {
        EXPECT_NEAR(sample, 0.75F, 1e-5F);
    }
}

/**
 * @brief Stereo source through Preserve mode passes each channel through independently.
 *
 * Every source frame is filled with (L=0.25, R=-0.5); Preserve mode must leave both
 * components untouched on the output. A regression that downmixed despite Preserve mode
 * would average the two to -0.125 and trip the EXPECT_NEAR on both interleaved slots.
 */
TEST(AudioPipelineReadTest, StereoPreserveModePropagatesIndependentChannels) {
    constexpr std::size_t kSourceFrames{2048};
    std::vector<float> samples(kSourceFrames * 2);
    for (std::size_t frame{0}; frame < kSourceFrames; ++frame) {
        samples[frame * 2 + 0] = 0.25F;
        samples[frame * 2 + 1] = -0.5F;
    }
    FakeAudioSource source{AudioFormat{
                               .channels   = 2,
                               .sampleRate = kBroadcastRateHz,
                           },
                           std::move(samples),
                           true};
    AudioPipeline pipeline(source,
                           AudioPipelineConfig{
                               .loop               = false,
                               .targetSampleRate   = kBroadcastRateHz,
                               .targetOutputFrames = 1024,
                               .channelMode        = AudioChannelMode::Preserve,
                           });

    std::vector<float> outputBlock(pipeline.outputSamplesPerBlock(), 99.0F);
    const auto status{pipeline.read(outputBlock)};

    ASSERT_EQ(status, AudioPipelineStatus::Ok);
    for (std::size_t sampleIdx{0}; sampleIdx + 1 < outputBlock.size(); sampleIdx += 2) {
        EXPECT_NEAR(outputBlock[sampleIdx + 0], 0.25F, 1e-5F);
        EXPECT_NEAR(outputBlock[sampleIdx + 1], -0.5F, 1e-5F);
    }
}

/**
 * @brief read() returns Error when the source already has its sticky error flag raised on the first read.
 *
 * FakeAudioSource publishes error()=true at the same call that returns 0 samples, so the
 * pipeline reader hits the error-during-zero-read path and reports Error rather than End -
 * distinguishing a fatal I/O failure from a clean EOF on the same zero-sample return.
 */
TEST(AudioPipelineReadTest, ReportsErrorWhenSourceErrorFlagIsSet) {
    FakeAudioSource source{AudioFormat{
                               .channels   = 1,
                               .sampleRate = kBroadcastRateHz,
                           },
                           true};
    source.setError(true);
    AudioPipeline pipeline(source,
                           AudioPipelineConfig{
                               .loop               = false,
                               .targetSampleRate   = kBroadcastRateHz,
                               .targetOutputFrames = 1024,
                               .channelMode        = AudioChannelMode::Mono,
                           });

    std::vector<float> outputBlock(pipeline.outputSamplesPerBlock(), 0.0F);

    EXPECT_EQ(pipeline.read(outputBlock), AudioPipelineStatus::Error);
}

/**
 * @brief Non-loop pipeline on an empty source returns End on the first read and stays at End thereafter.
 *
 * The first EXPECT pins down the empty short-circuit: no silence block or Ok with garbage on
 * a missing-input source. The second pins down the latched-End invariant: the drained flag
 * stays set and subsequent reads continue returning End instead of attempting to restart.
 */
TEST(AudioPipelineReadTest, NonLoopOnEmptySourceReturnsEndAndStays) {
    FakeAudioSource source{AudioFormat{
                               .channels   = 1,
                               .sampleRate = kBroadcastRateHz,
                           },
                           true};
    AudioPipeline pipeline(source,
                           AudioPipelineConfig{
                               .loop               = false,
                               .targetSampleRate   = kBroadcastRateHz,
                               .targetOutputFrames = 1024,
                               .channelMode        = AudioChannelMode::Mono,
                           });

    std::vector<float> outputBlock(pipeline.outputSamplesPerBlock(), 0.0F);

    EXPECT_EQ(pipeline.read(outputBlock), AudioPipelineStatus::End);
    EXPECT_EQ(pipeline.read(outputBlock), AudioPipelineStatus::End);
}

/**
 * @brief Loop mode replays content from a seekable source across multiple blocks without ever reporting End.
 *
 * Source content is intentionally shorter than a single output block (600 frames vs 1024
 * samples per call) so every read() requires at least one rewind to fill the block,
 * exercising the loop boundary path multiple times across the five iterations.
 */
TEST(AudioPipelineReadTest, LoopModeReplaysContent) {
    constexpr std::size_t kSourceFrames{600};
    constexpr std::size_t kIterations{5};
    std::vector<float> samples(kSourceFrames, 0.5F);
    FakeAudioSource source{AudioFormat{
                               .channels   = 1,
                               .sampleRate = kBroadcastRateHz,
                           },
                           std::move(samples),
                           true};
    AudioPipeline pipeline(source,
                           AudioPipelineConfig{
                               .loop               = true,
                               .targetSampleRate   = kBroadcastRateHz,
                               .targetOutputFrames = 1024,
                               .channelMode        = AudioChannelMode::Mono,
                           });

    std::vector<float> outputBlock(pipeline.outputSamplesPerBlock(), 0.0F);
    for (std::size_t blockIdx{0}; blockIdx < kIterations; ++blockIdx) {
        EXPECT_EQ(pipeline.read(outputBlock), AudioPipelineStatus::Ok);
    }
}

/**
 * @brief Loop mode on an empty seekable source reports End instead of livelocking on empty replays.
 *
 * Rewinding an empty source produces another empty read on every iteration - the SUT must
 * detect "no progress after rewind" and convert it to End rather than spinning forever
 * pulling zero samples through an unending sequence of rewinds.
 */
TEST(AudioPipelineReadTest, LoopModeReportsEndOnEmptySource) {
    FakeAudioSource source{AudioFormat{
                               .channels   = 1,
                               .sampleRate = kBroadcastRateHz,
                           },
                           true};
    AudioPipeline pipeline(source,
                           AudioPipelineConfig{
                               .loop               = true,
                               .targetSampleRate   = kBroadcastRateHz,
                               .targetOutputFrames = 1024,
                               .channelMode        = AudioChannelMode::Mono,
                           });

    std::vector<float> outputBlock(pipeline.outputSamplesPerBlock(), 0.0F);

    EXPECT_EQ(pipeline.read(outputBlock), AudioPipelineStatus::End);
}

/**
 * @brief Passthrough emits exactly the source frame count.
 *
 * Equal source/target rate routes AudioRateConverter to its passthrough fast path - no
 * libsoxr, no warmup / drain padding. With kSourceFrames a multiple of targetOutputFrames
 * block-rounding adds nothing either, pinning the total to kSourceFrames bit-for-bit -
 * a regression that spuriously built soxr on an equal-rate config trips the EXPECT_EQ that
 * AudioPipelineResampleSweepTest's 10% tolerance would mask.
 */
TEST(AudioPipelineReadTest, PassthroughEmitsExactSourceFrameCount) {
    constexpr std::size_t kSourceFrames{16'384};
    constexpr std::size_t kTargetOutputFrames{1'024};
    std::vector<float> samples(kSourceFrames, 0.5F);
    FakeAudioSource source{AudioFormat{
                               .channels   = 1,
                               .sampleRate = kBroadcastRateHz,
                           },
                           std::move(samples),
                           true};
    AudioPipeline pipeline(source,
                           AudioPipelineConfig{
                               .loop               = false,
                               .targetSampleRate   = kBroadcastRateHz,
                               .targetOutputFrames = kTargetOutputFrames,
                               .channelMode        = AudioChannelMode::Mono,
                           });

    std::vector<float> outputBlock(pipeline.outputSamplesPerBlock(), 0.0F);
    std::size_t totalSamples{0};
    while (true) {
        const auto status{pipeline.read(outputBlock)};
        if (status == AudioPipelineStatus::End) {
            break;
        }
        ASSERT_EQ(status, AudioPipelineStatus::Ok);
        totalSamples += outputBlock.size();
    }

    EXPECT_EQ(totalSamples, kSourceFrames);
}

namespace {
    struct ResampleTestCase {
        std::string_view name;
        int sourceRate;
        int targetRate;
    };

    /**
     * @brief Render a ResampleTestCase as its name plus the (sourceRate -> targetRate) pair
     *        for gtest listings.
     */
    void PrintTo(const ResampleTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {" << testCase.sourceRate << " -> " << testCase.targetRate << "}";
    }

    /**
     * @brief Three rate-converting regimes the pipeline must handle: the canonical 44.1 /
     *        48 kHz upsample, its symmetric downsample, and a large upsample into the
     *        project's broadcast ceiling. Same canonical pairing as the SoxrResampler and
     *        AudioRateConverter test suites. The same-rate passthrough configuration is
     *        covered exact-match by PassthroughEmitsExactSourceFrameCount instead - libsoxr is
     *        never instantiated on that path, so the 10% warmup / drain budget carried by the
     *        sweep below would be meaninglessly generous there.
     */
    std::vector<ResampleTestCase> makeResampleTestCases() {
        return {
            ResampleTestCase{"Upsample441kTo48k", kCdAudioRateHz, kBroadcastRateHz},
            ResampleTestCase{"Downsample48kTo441k", kBroadcastRateHz, kCdAudioRateHz},
            ResampleTestCase{"LargeUpsample441kTo192k", kCdAudioRateHz, kMaxSampleRateHz},
        };
    }
}  // namespace

class AudioPipelineResampleSweepTest : public ::testing::TestWithParam<ResampleTestCase> {};

/**
 * @brief Total output sample count is approximately sourceFrames * targetRate / sourceRate.
 *
 * The expected count derives from the rate ratio applied to the source frame budget; the 10%
 * tolerance absorbs libsoxr's warmup padding on the first block and drain padding on the
 * last block (each up to outputFrames silence samples). A regression that dropped input
 * mid-stream or emitted runaway blocks would exceed the tolerance and surface on the
 * EXPECT_NEAR.
 */
TEST_P(AudioPipelineResampleSweepTest, EmitsTargetRateBlocksWithoutInputDrop) {
    constexpr std::size_t kSourceFrames{16'384};
    const auto& testCase{GetParam()};
    std::vector<float> samples(kSourceFrames, 0.5F);
    FakeAudioSource source{AudioFormat{
                               .channels   = 1,
                               .sampleRate = testCase.sourceRate,
                           },
                           std::move(samples),
                           true};
    AudioPipeline pipeline(source,
                           AudioPipelineConfig{
                               .loop               = false,
                               .targetSampleRate   = testCase.targetRate,
                               .targetOutputFrames = 1024,
                               .channelMode        = AudioChannelMode::Mono,
                           });

    std::vector<float> outputBlock(pipeline.outputSamplesPerBlock(), 0.0F);
    std::size_t totalSamples{0};
    while (true) {
        const auto status{pipeline.read(outputBlock)};
        if (status == AudioPipelineStatus::End) {
            break;
        }
        ASSERT_EQ(status, AudioPipelineStatus::Ok);
        totalSamples += outputBlock.size();
    }
    const double expected{static_cast<double>(kSourceFrames) * testCase.targetRate / testCase.sourceRate};
    EXPECT_NEAR(static_cast<double>(totalSamples), expected, expected * 0.10);
}

INSTANTIATE_TEST_SUITE_P(RatioMatrix, AudioPipelineResampleSweepTest, ::testing::ValuesIn(makeResampleTestCases()),
                         [](const ::testing::TestParamInfo<ResampleTestCase>& info) {
                             return std::string{info.param.name};
                         });
