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
[[nodiscard]] bool validateMonoStereoAudioFormat(AudioFormat format, int minSampleRate, int maxSampleRate);

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
 * @brief Read fixed-size interleaved blocks from an AudioSource.
 *
 * The reader centralizes EOF, loop, and zero-padding policy:
 *   - normal full reads return Ok;
 *   - non-loop partial tail blocks are zero-padded and returned once;
 *   - loop partial tail blocks rewind immediately and continue reading,
 *     avoiding a silence gap at the file boundary;
 *   - a clean EOF before any new samples returns End.
 */
class AudioBlockReader {
public:
    /**
     * @brief Construct a fixed-shape block reader.
     *
     * @param source Source to read from; must outlive this reader.
     * @param channels Source channel count (>= 1).
     * @param framesPerBlock Frames per returned block (> 0).
     * @param loop Whether EOF should rewind and continue.
     */
    AudioBlockReader(AudioSource& source, int channels, int framesPerBlock, bool loop);

    AudioBlockReader(const AudioBlockReader&)            = delete;
    AudioBlockReader& operator=(const AudioBlockReader&) = delete;
    AudioBlockReader(AudioBlockReader&&)                 = delete;
    AudioBlockReader& operator=(AudioBlockReader&&)      = delete;

    [[nodiscard]] int channels() const;
    [[nodiscard]] int framesPerBlock() const;
    [[nodiscard]] std::size_t samplesPerBlock() const;

    /**
     * @brief Fill one interleaved block.
     *
     * @pre dst.size() == samplesPerBlock().
     *
     * @param dst Destination block; interleaved by source channel count.
     * @return Ok, End, or Error.
     */
    [[nodiscard]] AudioPipelineStatus read(std::span<float> dst);

private:
    AudioSource& source_;
    int channels_;
    int framesPerBlock_;
    bool loop_;
};

/**
 * @brief Downmix interleaved audio to mono with equal channel weighting.
 *
 * @pre interleaved.size() == mono.size() * channels.
 * @pre channels >= 1.
 *
 * @param interleaved Source samples.
 * @param channels Source channel count.
 * @param mono Destination mono frames.
 */
void downmixInterleavedToMono(std::span<const float> interleaved, int channels, std::span<float> mono);

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

    [[nodiscard]] AudioFormat sourceFormat() const;
    [[nodiscard]] int outputChannels() const;
    [[nodiscard]] int inputFrames() const;
    [[nodiscard]] int outputFrames() const;
    [[nodiscard]] std::size_t outputSamplesPerBlock() const;

    /**
     * @brief Read and convert one block at the target sample rate.
     *
     * @pre out.size() == outputSamplesPerBlock().
     *
     * @param out Destination block, interleaved by outputChannels().
     * @return Ok, End, or Error.
     */
    [[nodiscard]] AudioPipelineStatus read(std::span<float> out);

private:
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
