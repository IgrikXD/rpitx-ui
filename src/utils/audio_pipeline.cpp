/**
 * @file audio_pipeline.cpp
 * @brief Shared audio pipeline helper implementations.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "audio_pipeline.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <utility>

bool validateMonoStereoAudioFormat(AudioFormat format, int minSampleRate, int maxSampleRate) {
    if (format.channels != 1 && format.channels != 2) {
        std::cerr << "[ERROR] Input must be mono or stereo, got " << format.channels << " channels." << std::endl;
        return false;
    }

    if (format.sampleRate < minSampleRate || format.sampleRate > maxSampleRate) {
        std::cerr << "[ERROR] Input sample rate " << format.sampleRate << " Hz is outside the supported range ["
                  << minSampleRate << ", " << maxSampleRate << "]." << std::endl;
        return false;
    }

    return true;
}

bool validateLoopSupport(const AudioSource& source, bool loopRequested) {
    if (loopRequested && source.seekable() == false) {
        std::cerr << "[ERROR] Audio source is not seekable; -l (loop) is unsupported on this input." << std::endl;
        return false;
    }
    return true;
}

AudioBlockReader::AudioBlockReader(AudioSource& source, int channels, int framesPerBlock, bool loop)
    : source_{source}, channels_{channels}, framesPerBlock_{framesPerBlock}, loop_{loop} {
    assert(channels > 0);
    assert(framesPerBlock > 0);
}

int AudioBlockReader::channels() const {
    return channels_;
}

int AudioBlockReader::framesPerBlock() const {
    return framesPerBlock_;
}

std::size_t AudioBlockReader::samplesPerBlock() const {
    return static_cast<std::size_t>(channels_) * static_cast<std::size_t>(framesPerBlock_);
}

AudioPipelineStatus AudioBlockReader::read(std::span<float> dst) {
    assert(dst.size() == samplesPerBlock());

    std::size_t filledSamples{0};
    int consecutiveEofWithoutProgress{0};

    while (filledSamples < dst.size()) {
        const std::size_t beforeRead{filledSamples};
        const std::size_t samplesRead{source_.read(dst.subspan(filledSamples))};

        if (samplesRead > 0) {
            if (samplesRead % static_cast<std::size_t>(channels_) != 0) {
                return AudioPipelineStatus::Error;
            }
            filledSamples += samplesRead;
            consecutiveEofWithoutProgress = 0;
            continue;
        }

        if (source_.error()) {
            return AudioPipelineStatus::Error;
        }

        if (loop_ == false) {
            if (filledSamples == 0) {
                return AudioPipelineStatus::End;
            }
            std::fill(dst.begin() + static_cast<std::ptrdiff_t>(filledSamples), dst.end(), 0.0F);
            return AudioPipelineStatus::Ok;
        }

        ++consecutiveEofWithoutProgress;
        if (source_.rewind() == false) {
            return AudioPipelineStatus::Error;
        }

        // Avoid spinning forever on an empty or unreadable-after-rewind source.
        // Short-but-valid looped files make progress after each rewind, so they
        // never hit this guard.
        if (beforeRead == filledSamples && consecutiveEofWithoutProgress > 1) {
            if (filledSamples == 0) {
                return AudioPipelineStatus::End;
            }
            std::fill(dst.begin() + static_cast<std::ptrdiff_t>(filledSamples), dst.end(), 0.0F);
            return AudioPipelineStatus::Ok;
        }
    }

    return AudioPipelineStatus::Ok;
}

void downmixInterleavedToMono(std::span<const float> interleaved, int channels, std::span<float> mono) {
    assert(channels > 0);
    assert(interleaved.size() == mono.size() * static_cast<std::size_t>(channels));

    if (channels == 1) {
        std::copy(interleaved.begin(), interleaved.end(), mono.begin());
        return;
    }

    const auto chCount{static_cast<std::size_t>(channels)};
    const float scale{1.0F / static_cast<float>(channels)};
    for (std::size_t i{0}; i < mono.size(); ++i) {
        float sum{0.0F};
        for (std::size_t c{0}; c < chCount; ++c) {
            sum += interleaved[i * chCount + c];
        }
        mono[i] = sum * scale;
    }
}

AudioPipeline::AudioPipeline(AudioSource& source, AudioPipelineConfig config)
    : sourceFormat_{source.format()},
      config_{std::move(config)},
      outputChannels_{config_.channelMode == AudioChannelMode::Mono ? 1 : sourceFormat_.channels} {
    assert(sourceFormat_.channels > 0);
    assert(outputChannels_ > 0);

    rateConverters_.reserve(static_cast<std::size_t>(outputChannels_));
    for (int c{0}; c < outputChannels_; ++c) {
        rateConverters_.emplace_back(sourceFormat_.sampleRate, config_.targetSampleRate, config_.targetOutputFrames,
                                     config_.tapsPerPhase, config_.maxCutoffHz);
    }

    inputFrames_  = rateConverters_.front().inputFrames();
    outputFrames_ = rateConverters_.front().outputFrames();
    reader_.emplace(source, sourceFormat_.channels, inputFrames_, config_.loop);

    interleavedInput_.assign(reader_->samplesPerBlock(), 0.0F);
    channelInput_.assign(static_cast<std::size_t>(outputChannels_),
                         std::vector<float>(static_cast<std::size_t>(inputFrames_), 0.0F));
    channelOutput_.assign(static_cast<std::size_t>(outputChannels_),
                          std::vector<float>(static_cast<std::size_t>(outputFrames_), 0.0F));
}

AudioFormat AudioPipeline::sourceFormat() const {
    return sourceFormat_;
}

int AudioPipeline::outputChannels() const {
    return outputChannels_;
}

int AudioPipeline::inputFrames() const {
    return inputFrames_;
}

int AudioPipeline::outputFrames() const {
    return outputFrames_;
}

std::size_t AudioPipeline::outputSamplesPerBlock() const {
    return static_cast<std::size_t>(outputFrames_) * static_cast<std::size_t>(outputChannels_);
}

AudioPipelineStatus AudioPipeline::read(std::span<float> out) {
    assert(out.size() == outputSamplesPerBlock());
    assert(reader_ != std::nullopt);

    const auto status{reader_->read({interleavedInput_.data(), interleavedInput_.size()})};
    if (status != AudioPipelineStatus::Ok) {
        return status;
    }

    if (config_.channelMode == AudioChannelMode::Mono) {
        downmixInterleavedToMono(interleavedInput_, sourceFormat_.channels, channelInput_[0]);
    } else {
        for (int c{0}; c < outputChannels_; ++c) {
            auto& channel{channelInput_[static_cast<std::size_t>(c)]};
            for (int i{0}; i < inputFrames_; ++i) {
                channel[static_cast<std::size_t>(i)] =
                    interleavedInput_[static_cast<std::size_t>(i) * static_cast<std::size_t>(sourceFormat_.channels) +
                                      static_cast<std::size_t>(c)];
            }
        }
    }

    for (int c{0}; c < outputChannels_; ++c) {
        rateConverters_[static_cast<std::size_t>(c)].process(channelInput_[static_cast<std::size_t>(c)],
                                                             channelOutput_[static_cast<std::size_t>(c)]);
    }

    if (outputChannels_ == 1) {
        std::copy(channelOutput_[0].begin(), channelOutput_[0].end(), out.begin());
    } else {
        for (int i{0}; i < outputFrames_; ++i) {
            for (int c{0}; c < outputChannels_; ++c) {
                out[static_cast<std::size_t>(i) * static_cast<std::size_t>(outputChannels_) +
                    static_cast<std::size_t>(c)] = channelOutput_[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
            }
        }
    }

    return AudioPipelineStatus::Ok;
}
