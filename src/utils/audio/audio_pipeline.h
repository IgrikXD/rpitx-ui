/**
 * @file audio_pipeline.h
 * @brief Shared audio block reading, channel adaptation, and mono rate-conversion pipeline.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "audio_rate_converter.h"
#include "audio_source.h"

/**
 * @brief Result of pulling one audio block through a shared pipeline stage.
 */
enum class AudioPipelineStatus {
    Ok,     ///< A full output block is available.
    End,    ///< Clean end-of-stream before any new samples were read.
    Error,  ///< I/O, rewind, or format-contract failure.
};

/**
 * @brief Validate that an AudioSource format is mono / stereo and in a sample-rate range.
 *
 * Prints a concise diagnostic to stderr on failure.
 *
 * @param format        Source format to validate.
 * @param minSampleRate Inclusive minimum sample rate in Hz.
 * @param maxSampleRate Inclusive maximum sample rate in Hz.
 * @return true when the format is usable by the caller.
 */
[[nodiscard]] bool validateAudioFormat(AudioFormat format, int minSampleRate, int maxSampleRate);

/**
 * @brief Validate that a source can satisfy requested loop playback.
 *
 * Prints a concise diagnostic to stderr on failure.
 *
 * @param source        Source to inspect.
 * @param loopRequested Whether the caller requested loop playback.
 * @return true when playback can proceed.
 */
[[nodiscard]] bool validateLoopSupport(const AudioSource& source, bool loopRequested);

/**
 * @brief Channel-adaptation mode for AudioPipeline.
 */
enum class AudioChannelMode {
    Preserve,  ///< Keep source channels and resample each independently.
    Mono,      ///< Downmix all source channels to one mono channel.
};

/**
 * @brief Configuration for AudioPipeline.
 */
struct AudioPipelineConfig {
    bool loop;
    int targetSampleRate;
    int targetOutputFrames;
    AudioChannelMode channelMode;
};

/**
 * @brief AudioSource -> loop-aware block reader -> channel adapter -> rate converter.
 *
 * Produces fixed-size interleaved float blocks at the caller's target sample
 * rate. Mono-only transmitters request AudioChannelMode::Mono; stereo-aware
 * processors such as pifmrds request AudioChannelMode::Preserve.
 */
class AudioPipeline {
public:
    /**
     * @brief Construct the pipeline from an already-open source.
     *
     * @param source Source to read from; must outlive this pipeline.
     * @param config Pipeline policy and target block geometry.
     *
     * @throws std::invalid_argument when the source channel count is non-positive,
     *         when an internal AudioRateConverter rejects the rate parameters
     *         (non-positive rates, non-positive output frames), or when the
     *         derived per-call input frame count overflows int.
     * @throws std::runtime_error    when an internal AudioRateConverter cannot
     *         create its libsoxr backend.
     */
    AudioPipeline(AudioSource& source, AudioPipelineConfig config);

    AudioPipeline(const AudioPipeline&)            = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;
    AudioPipeline(AudioPipeline&&)                 = delete;
    AudioPipeline& operator=(AudioPipeline&&)      = delete;

    [[nodiscard]] int outputChannels() const noexcept {
        return outputChannels_;
    }
    [[nodiscard]] int outputFrames() const noexcept {
        return outputFrames_;
    }
    [[nodiscard]] std::size_t outputSamplesPerBlock() const noexcept {
        return static_cast<std::size_t>(outputFrames_) * static_cast<std::size_t>(outputChannels_);
    }

    /**
     * @brief Read and convert one block at the target sample rate.
     *
     * Returns Error when out.size() does not equal outputSamplesPerBlock().
     *
     * @param out Destination block, interleaved by outputChannels().
     * @return Ok, End, or Error.
     *
     * @throws std::invalid_argument when an internal invariant is violated
     *         (e.g. mono downmix called with mismatched span sizes); these
     *         indicate a programming error rather than a stream condition.
     */
    [[nodiscard]] AudioPipelineStatus read(std::span<float> out);

private:
    class AudioBlockReader {
    public:
        AudioBlockReader(AudioSource& source, int channels, bool loop);

        AudioBlockReader(const AudioBlockReader&)            = delete;
        AudioBlockReader& operator=(const AudioBlockReader&) = delete;
        AudioBlockReader(AudioBlockReader&&)                 = delete;
        AudioBlockReader& operator=(AudioBlockReader&&)      = delete;

        /**
         * @brief Read exactly dst.size() samples from the source.
         *
         * Block size is decided per-call by the caller (the rate converter's
         * Bresenham accumulator may demand a different input frame count
         * between adjacent calls), so this reader is rate-agnostic and only
         * enforces channel alignment.
         *
         * In loop mode the reader never mixes end-of-file and start-of-file
         * content within a single block: when the source exhausts mid-read,
         * the unfilled tail is zero-padded and a rewind is deferred to the
         * next call. The pipeline can then reset the converter filter state
         * before processing the post-rewind block by checking
         * consumeLoopBoundary().
         *
         * @param dst Destination buffer; size must be a positive multiple
         *            of the channel count established at construction.
         */
        [[nodiscard]] AudioPipelineStatus read(std::span<float> dst);

        /**
         * @brief Whether the most recent read() performed a loop rewind.
         *
         * Returns true exactly once after a read that started by rewinding
         * the source - the caller should reset rate-converter filter state
         * before processing that block. Subsequent calls return false until
         * another rewind happens.
         */
        [[nodiscard]] bool consumeLoopBoundary() noexcept {
            const bool result{restartedThisRead_};
            restartedThisRead_ = false;
            return result;
        }

    private:
        AudioSource& source_;
        int channels_;
        bool loop_;
        bool rewindPending_{false};      ///< Set when EOF was reached mid-read in loop mode.
        bool restartedThisRead_{false};  ///< Set when read() began with a deferred rewind.
    };

    /**
     * @brief Drive a draining block: pull each converter's tail into out.
     *
     * Called after the block reader has reported End in non-loop mode so
     * libsoxr's filter delay line is recovered instead of being discarded
     * with the source. Returns Ok with a partial-then-padded block while
     * any converter still has tail samples, End once all converters are
     * fully drained.
     */
    [[nodiscard]] AudioPipelineStatus drainBlock(std::span<float> out);

    AudioFormat sourceFormat_;
    AudioPipelineConfig config_;
    int outputChannels_;
    int maxInputFrames_{0};
    int outputFrames_{0};
    std::optional<AudioBlockReader> reader_;
    std::vector<AudioRateConverter> rateConverters_;
    std::vector<float> interleavedInput_;
    std::vector<std::vector<float>> channelInput_;
    std::vector<std::vector<float>> channelOutput_;
    bool draining_{false};  ///< Switched on after the source reports End in non-loop mode.
    bool drained_{false};   ///< Set once drainBlock() has returned all tail samples.
};
