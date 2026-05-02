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
     * @param out Destination block, interleaved by outputChannels().
     * @return Ok on a normal block, End once the source and converter
     *         tails are exhausted, or Error on an underlying source / rate
     *         converter runtime failure.
     *
     * @throws std::invalid_argument if out.size() does not equal
     *         outputSamplesPerBlock() (caller contract violation).
     * @throws std::logic_error on an internal invariant violation
     *         (e.g. converter requesting more input than the buffer can
     *         hold, source returning a partial frame).
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
         * Block size is chosen per-call by the caller; this reader is
         * rate-agnostic and only enforces channel alignment.
         *
         * In loop mode reads are gap-free: on EOF the source is rewound
         * in place and the same block keeps filling. A short linear
         * crossfade (kCrossfadeFrames) smooths mid-block seams; on a
         * block-aligned EOF consumeLoopBoundary() reports true so the
         * converter filter state can be reset cleanly.
         *
         * @param dst Destination buffer; size must be a positive multiple
         *            of the channel count established at construction.
         *
         * @throws std::invalid_argument if dst is empty or its size is not
         *         a multiple of the channel count.
         * @throws std::logic_error if the source returns a partial frame.
         */
        [[nodiscard]] AudioPipelineStatus read(std::span<float> dst);

        /**
         * @brief Whether the most recent read() began at a clean loop boundary.
         *
         * Returns true exactly once after a read whose first samples came
         * from a rewind (no pre-rewind tail in the block). Also returns
         * true once when a previous read() rewound the source but had to
         * exit before any post-rewind samples were available - in that
         * case the signal is deferred to the next call so the caller
         * resets the converter before processing fresh-loop data, not
         * before the trailing pre-rewind tail still queued in the
         * converter's filter state.
         */
        [[nodiscard]] bool consumeLoopBoundary() noexcept {
            const bool result{restartedThisRead_ || pendingLoopBoundary_};
            restartedThisRead_   = false;
            pendingLoopBoundary_ = false;
            return result;
        }

    private:
        /// Crossfade window in frames used to smooth mid-block loop seams.
        /// 32 frames maps to ~0.17 ms @ 192 kHz and ~4 ms @ 8 kHz: long
        /// enough to mask the filter discontinuity, short enough to be
        /// inaudible against the surrounding signal.
        static constexpr std::size_t kCrossfadeFrames{32};

        AudioSource& source_;
        int channels_;
        bool loop_;
        bool restartedThisRead_{false};       ///< Set when read() filled the block starting from a rewind.
        bool pendingLoopBoundary_{false};     ///< Carries an unsignalled rewind from one read() into the next.
        std::vector<float> crossfadeBuffer_;  ///< Scratch for post-rewind head samples (loop mode only).
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
