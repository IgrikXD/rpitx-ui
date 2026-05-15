/**
 * @file test_libsndfile_audio_source.cpp
 * @brief Unit tests for the libsndfile-backed AudioSource factory.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>
#include <sndfile.h>

#include <cstddef>
#include <filesystem>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "captured_streams_mixin.h"
#include "libsndfile_audio_source.h"

namespace {
    /**
     * @brief CD-audio sample rate (44.1 kHz). Paired with stereo on the open-format case.
     */
    constexpr int kCdAudioRateHz{44'100};

    /**
     * @brief Broadcast sample rate (48 kHz). Default for the mono open-format case and
     *        for every read / rewind / dispatcher fixture.
     */
    constexpr int kBroadcastRateHz{48'000};

    /**
     * @brief Default fixture-WAV frame count. 256 frames keeps the on-disk PCM_16 WAV
     *        in the low-kilobyte range (~0.5 KiB mono, ~1 KiB stereo) - cheap enough to
     *        write per test yet large enough to materialize a non-degenerate sample
     *        buffer for both channel layouts.
     */
    constexpr int kDefaultFixtureFrames{256};

    /**
     * @brief Write a PCM_16 WAV fixture (mono or stereo) to a unique temp path.
     *
     * Each call writes a fresh file at `<temp_dir>/rpitx_test_<random>.wav` so parallel
     * ctest workers do not clash on the same path. The sample content is a deterministic
     * linear ramp in [0, 0.5] - low-amplitude, well within libsndfile's PCM_16 -> float
     * range and adjacent-frame-distinct so a rewind comparison can detect any non-zero
     * seek offset.
     *
     * @throws std::runtime_error On sf_open failure or short write.
     */
    [[nodiscard]] std::string writeTempWav(int channels, int sampleRate, int frames) {
        SF_INFO info{};
        info.channels   = channels;
        info.samplerate = sampleRate;
        info.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

        std::random_device randomDevice;
        const auto path{
            (std::filesystem::temp_directory_path() / ("rpitx_test_" + std::to_string(randomDevice()) + ".wav"))
                .string()};

        SNDFILE* handle{sf_open(path.c_str(), SFM_WRITE, &info)};
        if (handle == nullptr) {
            throw std::runtime_error{std::string{"writeTempWav: sf_open failed: "} + sf_strerror(nullptr)};
        }

        std::vector<float> samples(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels));
        for (std::size_t n{0}; n < static_cast<std::size_t>(frames); ++n) {
            const float sample{static_cast<float>(n) / static_cast<float>(frames) * 0.5F};
            for (int c{0}; c < channels; ++c) {
                samples[n * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)] = sample;
            }
        }
        const sf_count_t written{sf_writef_float(handle, samples.data(), frames)};
        sf_close(handle);
        if (written != frames) {
            throw std::runtime_error{"writeTempWav: short write"};
        }
        return path;
    }

    /**
     * @brief RAII guard that removes the file on destruction.
     *
     * Keeps ctest's temp directory clean across hundreds of unit-test runs instead of
     * relying on the operating system's eventual GC of the temp tree.
     */
    struct TempFileGuard {
        std::string path;
        ~TempFileGuard() {
            if (path.empty() == false) {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        }
    };

}  // namespace

class MakeFileAudioSourceFailureTest : public CapturedStreamsMixin<::testing::Test> {};

/**
 * @brief makeFileAudioSource returns nullptr and emits a stderr diagnostic for a nonexistent path.
 *
 * The failure-path contract is two-fold: the factory surfaces the error through a nullptr
 * return so callers can branch on a simple null check instead of a try/catch, and through
 * a stderr diagnostic so a CLI launch failure is visible to the operator. Asserting that
 * stderr is non-empty rather than checking its exact content keeps the test stable across
 * libsndfile release notes - the surviving contract is "tells the user something went
 * wrong," not the precise wording of sf_strerror.
 */
TEST_F(MakeFileAudioSourceFailureTest, ReturnsNullptrAndPrintsDiagnosticForNonexistentPath) {
    const auto audioSource{makeFileAudioSource("/this/path/does/not/exist/at/all.wav")};

    EXPECT_EQ(audioSource, nullptr);
    EXPECT_FALSE(capturedStderr().empty());
}

namespace {
    struct OpenFormatTestCase {
        std::string_view name;
        int channels;
        int sampleRateHz;
    };

    /**
     * @brief Render an open-format case as its name and the (channels, sampleRateHz) pair
     *        for gtest listings and failure messages.
     */
    void PrintTo(const OpenFormatTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {channels=" << testCase.channels << ", sampleRateHz=" << testCase.sampleRateHz << "}";
    }

    /**
     * @brief Representative channel / rate pairs the file factory must open and report
     *        correctly: a mono broadcast WAV and a stereo CD-rate WAV. Two cases cover
     *        the two channel layouts the project's audio path uses in practice; the
     *        broadcast / CD pairing is the same canonical pairing used by the
     *        SoxrResampler and AudioRateConverter test suites.
     */
    std::vector<OpenFormatTestCase> makeOpenFormatTestCases() {
        return {
            OpenFormatTestCase{"MonoBroadcast", 1, kBroadcastRateHz},
            OpenFormatTestCase{"StereoCd", 2, kCdAudioRateHz},
        };
    }
}  // namespace

class MakeFileAudioSourceOpenTest : public ::testing::TestWithParam<OpenFormatTestCase> {};

/**
 * @brief Opening a valid WAV exposes the per-case format plus the universal success invariants.
 *
 * Per-case half (channels, sampleRate) verifies the ctor wired SF_INFO through into
 * AudioFormat for each input pair. Universal half (seekable() true, error() false,
 * description() non-empty) holds for any regular-file backing produced by libsndfile and
 * is therefore checked on both cases as a bundle rather than split into a parallel test.
 * A regression that hardcoded a wrong channel count or sample rate trips exactly one of
 * the two cases, isolating the misrouted axis; a universal-invariant regression (e.g.
 * always-false seekable) trips both uniformly - still surfaced, but without per-case
 * attribution.
 */
TEST_P(MakeFileAudioSourceOpenTest, ExposesFormatAndSuccessInvariantsForValidWav) {
    const auto& testCase{GetParam()};
    TempFileGuard guard{writeTempWav(testCase.channels, testCase.sampleRateHz, kDefaultFixtureFrames)};
    const auto audioSource{makeFileAudioSource(guard.path)};
    ASSERT_NE(audioSource, nullptr);

    const auto audioFormat{audioSource->format()};

    EXPECT_EQ(audioFormat.channels, testCase.channels);
    EXPECT_EQ(audioFormat.sampleRate, testCase.sampleRateHz);
    EXPECT_TRUE(audioSource->seekable());
    EXPECT_FALSE(audioSource->error());
    EXPECT_FALSE(audioSource->description().empty());
}

INSTANTIATE_TEST_SUITE_P(ChannelRatePairs, MakeFileAudioSourceOpenTest, ::testing::ValuesIn(makeOpenFormatTestCases()),
                         [](const ::testing::TestParamInfo<OpenFormatTestCase>& info) {
                             return std::string{info.param.name};
                         });

/**
 * @brief read() fills the destination buffer with floats clamped to [-1, 1].
 *
 * Sizing the destination exactly to the fixture frame count lets the test assert a
 * full-consume (samplesRead == destination.size()) before the range-check loop - without
 * that, a buggy "always return 0" SUT would let the loop trivially pass against the
 * default-initialized zeros and miss the regression. The fixture writes a [0, 0.5]
 * linear ramp, well within the SUT's sanitize-and-clamp range, so a raw-int16 leak from
 * a buggy format conversion would produce out-of-range values and trip the loop.
 */
TEST(MakeFileAudioSourceTest, ReadProducesNormalizedFloats) {
    TempFileGuard guard{writeTempWav(1, kBroadcastRateHz, kDefaultFixtureFrames)};
    const auto audioSource{makeFileAudioSource(guard.path)};
    ASSERT_NE(audioSource, nullptr);
    std::vector<float> destinationSamples(static_cast<std::size_t>(kDefaultFixtureFrames), 0.0F);

    const auto samplesRead{audioSource->read(destinationSamples)};

    EXPECT_EQ(samplesRead, destinationSamples.size());
    for (float sample: destinationSamples) {
        EXPECT_GE(sample, -1.0F);
        EXPECT_LE(sample, 1.0F);
    }
}

/**
 * @brief read() throws std::invalid_argument on an empty destination span.
 *
 * The empty-span branch of the SUT contract surfaces a programmer error - a caller that
 * forgot to size its buffer before calling read() - via throw rather than a sticky error
 * flag, so the fault propagates loudly through the test harness instead of being masked
 * under generic "source failed" telemetry. Exercising against a mono fixture isolates the
 * empty branch from the channel-alignment branch (covered by
 * ReadThrowsOnUnalignedDestinationSize).
 */
TEST(MakeFileAudioSourceTest, ReadThrowsOnEmptyDestination) {
    TempFileGuard guard{writeTempWav(1, kBroadcastRateHz, kDefaultFixtureFrames)};
    const auto audioSource{makeFileAudioSource(guard.path)};
    ASSERT_NE(audioSource, nullptr);
    std::vector<float> emptyDestination;

    EXPECT_THROW(
        { [[maybe_unused]] const std::size_t samplesRead{audioSource->read(emptyDestination)}; },
        std::invalid_argument);
}

/**
 * @brief read() throws std::invalid_argument when destination size is not a multiple of channels.
 *
 * The alignment branch of the SUT contract is exercised against a stereo fixture (channels=2)
 * with a 7-element destination: 7 % 2 != 0 trips the rejection. An odd buffer size against
 * a mono fixture would always satisfy size % 1 == 0 and miss this branch entirely - the
 * stereo fixture is what makes the test discriminating.
 */
TEST(MakeFileAudioSourceTest, ReadThrowsOnUnalignedDestinationSize) {
    TempFileGuard guard{writeTempWav(2, kBroadcastRateHz, kDefaultFixtureFrames)};
    const auto audioSource{makeFileAudioSource(guard.path)};
    ASSERT_NE(audioSource, nullptr);
    constexpr std::size_t kUnalignedDestinationSize{7};
    std::vector<float> unalignedDestination(kUnalignedDestinationSize, 0.0F);

    EXPECT_THROW(
        { [[maybe_unused]] const std::size_t samplesRead{audioSource->read(unalignedDestination)}; },
        std::invalid_argument);
}

/**
 * @brief rewind() restores the read position so a second pass matches the first byte-for-byte.
 *
 * Both passes read the same number of frames into separately-allocated buffers and the
 * comparison is via vector equality. Byte-for-byte equality is a stricter contract than
 * "rewind seeked somewhere earlier" - it catches a regression where the seek lands at a
 * non-zero offset (e.g. header skip miscounted) by failing on the very first differing
 * sample. The fixture's linear-ramp content guarantees adjacent frames differ, so any
 * non-zero seek offset produces an immediate mismatch instead of accidentally aliasing
 * into the same data.
 */
TEST(MakeFileAudioSourceTest, RewindRestartsReadFromTheBeginning) {
    TempFileGuard guard{writeTempWav(1, kBroadcastRateHz, kDefaultFixtureFrames)};
    const auto audioSource{makeFileAudioSource(guard.path)};
    ASSERT_NE(audioSource, nullptr);
    std::vector<float> firstPassSamples(static_cast<std::size_t>(kDefaultFixtureFrames), 0.0F);
    std::vector<float> secondPassSamples(static_cast<std::size_t>(kDefaultFixtureFrames), 0.0F);

    EXPECT_EQ(audioSource->read(firstPassSamples), firstPassSamples.size());
    EXPECT_TRUE(audioSource->rewind());
    EXPECT_EQ(audioSource->read(secondPassSamples), secondPassSamples.size());
    EXPECT_EQ(firstPassSamples, secondPassSamples);
}

/**
 * @brief makeAudioSource with useStdin=false routes to makeFileAudioSource and produces a usable source.
 *
 * The dispatcher's false-branch is a thin tail call into the file factory, so the smallest
 * stable signal that the dispatch wiring is intact is a non-null return on the same input
 * makeFileAudioSource would accept. Re-asserting the full format / metadata contract here
 * would duplicate ExposesFormatAndSuccessInvariantsForValidWav; the dispatcher is only
 * responsible for routing, not for re-validating libsndfile's output.
 */
TEST(MakeAudioSourceTest, DispatchesToFileFactoryWhenStdinFlagFalse) {
    TempFileGuard guard{writeTempWav(1, kBroadcastRateHz, kDefaultFixtureFrames)};

    const auto audioSource{makeAudioSource(false, guard.path)};

    EXPECT_NE(audioSource, nullptr);
}
