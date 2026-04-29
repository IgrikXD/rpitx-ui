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

bool validateAudioFormat(AudioFormat format, int minSampleRate, int maxSampleRate) {
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

AudioPipeline::AudioBlockReader::AudioBlockReader(AudioSource& source, int channels, bool loop)
    : source_{source}, channels_{channels}, loop_{loop} {
    assert(channels > 0);
}

AudioPipelineStatus AudioPipeline::AudioBlockReader::read(std::span<float> dst) {
    if (dst.empty() || dst.size() % static_cast<std::size_t>(channels_) != 0) {
        return AudioPipelineStatus::Error;
    }

    // Apply a deferred rewind from a previous loop-mode EOF, so the buffer
    // about to be filled contains only post-rewind audio. The pipeline reads
    // restartedThisRead_ via consumeLoopBoundary() after this call to decide
    // whether to reset the rate-converter filter state.
    restartedThisRead_ = false;
    if (rewindPending_) {
        if (source_.rewind() == false) {
            return AudioPipelineStatus::Error;
        }
        rewindPending_     = false;
        restartedThisRead_ = true;
    }

    std::size_t filledSamples{0};

    while (filledSamples < dst.size()) {
        const std::size_t samplesRead{source_.read(dst.subspan(filledSamples))};

        if (samplesRead > 0) {
            if (samplesRead % static_cast<std::size_t>(channels_) != 0) {
                return AudioPipelineStatus::Error;
            }
            filledSamples += samplesRead;
            if (source_.error()) {
                return AudioPipelineStatus::Error;
            }
            continue;
        }

        if (source_.error()) {
            return AudioPipelineStatus::Error;
        }

        // EOF reached. In non-loop mode this is the terminal state.
        if (loop_ == false) {
            if (filledSamples == 0) {
                return AudioPipelineStatus::End;
            }
            std::fill(dst.begin() + static_cast<std::ptrdiff_t>(filledSamples), dst.end(), 0.0F);
            return AudioPipelineStatus::Ok;
        }

        // Loop mode and EOF: close out this block (zero-padding the tail)
        // and defer the rewind to the next call so end-of-file and
        // start-of-file content never coexist in a single input span
        // passed to the rate converter.
        if (filledSamples > 0) {
            rewindPending_ = true;
            std::fill(dst.begin() + static_cast<std::ptrdiff_t>(filledSamples), dst.end(), 0.0F);
            return AudioPipelineStatus::Ok;
        }

        // filledSamples == 0: the source ran out before yielding anything
        // for this block. If we already rewound at the start of this call
        // and still got nothing the file is empty - report End so the
        // pipeline can stop cleanly instead of spinning.
        if (restartedThisRead_) {
            return AudioPipelineStatus::End;
        }

        // Otherwise the previous read happened to align exactly with EOF
        // (no padding was needed, so no rewind was deferred). Rewind in
        // place, mark the boundary, and continue the fill loop with fresh
        // post-rewind data.
        if (source_.rewind() == false) {
            return AudioPipelineStatus::Error;
        }
        restartedThisRead_ = true;
    }

    return AudioPipelineStatus::Ok;
}

bool AudioPipeline::AudioBlockReader::consumeLoopBoundary() {
    const bool result{restartedThisRead_};
    restartedThisRead_ = false;
    return result;
}

namespace {
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
}  // namespace

AudioPipeline::AudioPipeline(AudioSource& source, AudioPipelineConfig config)
    : sourceFormat_{source.format()},
      config_{std::move(config)},
      outputChannels_{config_.channelMode == AudioChannelMode::Mono ? 1 : sourceFormat_.channels} {
    assert(sourceFormat_.channels > 0);
    assert(outputChannels_ > 0);

    rateConverters_.reserve(static_cast<std::size_t>(outputChannels_));
    for (int c{0}; c < outputChannels_; ++c) {
        rateConverters_.emplace_back(sourceFormat_.sampleRate,
                                     config_.targetSampleRate,
                                     config_.targetOutputFrames);
    }

    // Per-channel converters are configured identically and start their
    // Bresenham accumulators from the same value, so any one of them is
    // representative of the whole channel array.
    maxInputFrames_ = rateConverters_.front().maxInputFrames();
    outputFrames_   = rateConverters_.front().outputFrames();
    reader_.emplace(source, sourceFormat_.channels, config_.loop);

    // Buffers are sized for the worst-case input block. The actual per-call
    // read uses the converter's peekNextInputFrames(), which never exceeds
    // maxInputFrames_, and the surplus capacity is simply unused that call.
    interleavedInput_.assign(static_cast<std::size_t>(maxInputFrames_) *
                                 static_cast<std::size_t>(sourceFormat_.channels),
                             0.0F);
    channelInput_.assign(static_cast<std::size_t>(outputChannels_),
                         std::vector<float>(static_cast<std::size_t>(maxInputFrames_), 0.0F));
    channelOutput_.assign(static_cast<std::size_t>(outputChannels_),
                          std::vector<float>(static_cast<std::size_t>(outputFrames_), 0.0F));
}

int AudioPipeline::outputChannels() const {
    return outputChannels_;
}

int AudioPipeline::outputFrames() const {
    return outputFrames_;
}

std::size_t AudioPipeline::outputSamplesPerBlock() const {
    return static_cast<std::size_t>(outputFrames_) * static_cast<std::size_t>(outputChannels_);
}

AudioPipelineStatus AudioPipeline::read(std::span<float> out) {
    if (out.size() != outputSamplesPerBlock() || reader_ == std::nullopt) {
        return AudioPipelineStatus::Error;
    }
    if (drained_) {
        return AudioPipelineStatus::End;
    }
    if (draining_) {
        return drainBlock(out);
    }

    // All converters share the same rate parameters and Bresenham state so
    // they require the same input frame count on every call; query just one.
    const int inputFramesThisCall{rateConverters_.front().peekNextInputFrames()};
    const std::size_t interleavedSize{static_cast<std::size_t>(inputFramesThisCall) *
                                      static_cast<std::size_t>(sourceFormat_.channels)};
    assert(interleavedSize <= interleavedInput_.size());

    const auto status{reader_->read({interleavedInput_.data(), interleavedSize})};
    if (status == AudioPipelineStatus::Error) {
        return AudioPipelineStatus::Error;
    }
    if (status == AudioPipelineStatus::End) {
        // Source ran out before yielding any new samples. Switch to drain
        // mode so the converter tails surface in subsequent calls instead
        // of being discarded together with the input stream.
        draining_ = true;
        return drainBlock(out);
    }

    // The reader may have just performed a deferred rewind to start a fresh
    // loop iteration. Reset filter state on every converter (preserving the
    // Bresenham accumulator) so the previous iteration's filter tail does
    // not smear into the start of this block.
    if (reader_->consumeLoopBoundary()) {
        for (auto& converter: rateConverters_) {
            converter.reset();
        }
    }

    if (config_.channelMode == AudioChannelMode::Mono) {
        downmixInterleavedToMono(
            std::span<const float>{interleavedInput_.data(), interleavedSize},
            sourceFormat_.channels,
            std::span<float>{channelInput_[0].data(), static_cast<std::size_t>(inputFramesThisCall)});
    } else {
        for (int c{0}; c < outputChannels_; ++c) {
            auto& channel{channelInput_[static_cast<std::size_t>(c)]};
            for (int i{0}; i < inputFramesThisCall; ++i) {
                channel[static_cast<std::size_t>(i)] =
                    interleavedInput_[static_cast<std::size_t>(i) * static_cast<std::size_t>(sourceFormat_.channels) +
                                      static_cast<std::size_t>(c)];
            }
        }
    }

    for (int c{0}; c < outputChannels_; ++c) {
        const bool converted{rateConverters_[static_cast<std::size_t>(c)].process(
            std::span<const float>{channelInput_[static_cast<std::size_t>(c)].data(),
                                   static_cast<std::size_t>(inputFramesThisCall)},
            std::span<float>{channelOutput_[static_cast<std::size_t>(c)].data(),
                             static_cast<std::size_t>(outputFrames_)})};
        if (converted == false) {
            return AudioPipelineStatus::Error;
        }
    }

    if (outputChannels_ == 1) {
        std::copy(channelOutput_[0].begin(), channelOutput_[0].end(), out.begin());
    } else {
        for (int i{0}; i < outputFrames_; ++i) {
            for (int c{0}; c < outputChannels_; ++c) {
                out[static_cast<std::size_t>(i) * static_cast<std::size_t>(outputChannels_) +
                    static_cast<std::size_t>(c)] =
                    channelOutput_[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
            }
        }
    }

    return AudioPipelineStatus::Ok;
}

AudioPipelineStatus AudioPipeline::drainBlock(std::span<float> out) {
    // Pull each converter's tail (spilled samples plus libsoxr filter
    // residue) into its channel buffer and zero-pad the unused slots so
    // the interleave step below treats every channel uniformly. All
    // converters share the same filter geometry and processed history, so
    // they always drain the same per-block sample count - tracking just
    // one channel's count is sufficient to decide when we are fully done.
    std::size_t producedAnyChannel{0};
    for (int c{0}; c < outputChannels_; ++c) {
        auto& channel{channelOutput_[static_cast<std::size_t>(c)]};
        const auto produced{rateConverters_[static_cast<std::size_t>(c)].drain(
            std::span<float>{channel.data(), static_cast<std::size_t>(outputFrames_)})};
        if (produced == std::nullopt) {
            return AudioPipelineStatus::Error;
        }
        const std::size_t produced_n{produced.value()};
        if (produced_n > producedAnyChannel) {
            producedAnyChannel = produced_n;
        }
        if (produced_n < static_cast<std::size_t>(outputFrames_)) {
            std::fill(channel.begin() + static_cast<std::ptrdiff_t>(produced_n), channel.end(), 0.0F);
        }
    }

    if (producedAnyChannel == 0) {
        drained_ = true;
        return AudioPipelineStatus::End;
    }

    if (outputChannels_ == 1) {
        std::copy(channelOutput_[0].begin(), channelOutput_[0].end(), out.begin());
    } else {
        for (int i{0}; i < outputFrames_; ++i) {
            for (int c{0}; c < outputChannels_; ++c) {
                out[static_cast<std::size_t>(i) * static_cast<std::size_t>(outputChannels_) +
                    static_cast<std::size_t>(c)] =
                    channelOutput_[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
            }
        }
    }

    return AudioPipelineStatus::Ok;
}
