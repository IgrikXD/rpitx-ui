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

#include "audio_source.h"
#include "polyphase_resampler.h"

/**
 * @brief Result of pulling one audio block through a shared pipeline stage.
 */
enum class AudioPipelineStatus {
    Ok,     ///< A full output block is available.
    End,    ///< Clean end-of-stream before any new samples were read.
    Error,  ///< I/O, rewind, or format-contract failure.
};

/**
 * @brief Validate that an AudioSource format is mono/stereo and in a sample-rate range.
 *
 * Prints a concise diagnostic to stderr on failure.
 *
 * @param format Source format to validate.
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
 * @param source Source to inspect.
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
    int tapsPerPhase;
    float maxCutoffHz;
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
     */
    AudioPipeline(AudioSource& source, AudioPipelineConfig config);

    AudioPipeline(const AudioPipeline&)            = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;
    AudioPipeline(AudioPipeline&&)                 = delete;
    AudioPipeline& operator=(AudioPipeline&&)      = delete;

    [[nodiscard]] int outputChannels() const;
    [[nodiscard]] int outputFrames() const;
    [[nodiscard]] std::size_t outputSamplesPerBlock() const;

    /**
     * @brief Read and convert one block at the target sample rate.
     *
     * Returns Error when out.size() does not equal outputSamplesPerBlock().
     *
     * @param out Destination block, interleaved by outputChannels().
     * @return Ok, End, or Error.
     */
    [[nodiscard]] AudioPipelineStatus read(std::span<float> out);

private:
    class AudioBlockReader {
    public:
        AudioBlockReader(AudioSource& source, int channels, int framesPerBlock, bool loop);

        AudioBlockReader(const AudioBlockReader&)            = delete;
        AudioBlockReader& operator=(const AudioBlockReader&) = delete;
        AudioBlockReader(AudioBlockReader&&)                 = delete;
        AudioBlockReader& operator=(AudioBlockReader&&)      = delete;

        [[nodiscard]] std::size_t samplesPerBlock() const;
        [[nodiscard]] AudioPipelineStatus read(std::span<float> dst);

    private:
        AudioSource& source_;
        int channels_;
        int framesPerBlock_;
        bool loop_;
    };

    AudioFormat sourceFormat_;
    AudioPipelineConfig config_;
    int outputChannels_;
    int inputFrames_{0};
    int outputFrames_{0};
    std::optional<AudioBlockReader> reader_;
    std::vector<AudioRateConverter> rateConverters_;
    std::vector<float> interleavedInput_;
    std::vector<std::vector<float>> channelInput_;
    std::vector<std::vector<float>> channelOutput_;
};
