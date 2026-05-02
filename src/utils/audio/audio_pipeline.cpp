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
#include <iostream>
#include <stdexcept>
#include <string>
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
        std::cerr << "[ERROR] Audio source is not seekable; Loop is unsupported on this input." << std::endl;
        return false;
    }
    return true;
}

AudioPipeline::AudioBlockReader::AudioBlockReader(AudioSource& source, int channels, bool loop)
    : source_{source}, channels_{channels}, loop_{loop} {
    if (channels <= 0) {
        throw std::invalid_argument{"AudioBlockReader: channels must be positive, got " + std::to_string(channels)};
    }
    if (loop_) {
        crossfadeBuffer_.assign(kCrossfadeFrames * static_cast<std::size_t>(channels_), 0.0F);
    }
}

AudioPipelineStatus AudioPipeline::AudioBlockReader::read(std::span<float> dst) {
    // Caller contract: dst must be a non-empty whole number of frames.
    // Surface as throw (consistent with downmixInterleavedToMono and the
    // ctors below) so a programmer error never silently degrades to a
    // runtime status code.
    if (dst.empty() || dst.size() % static_cast<std::size_t>(channels_) != 0) {
        throw std::invalid_argument{"AudioBlockReader::read: dst must be a non-empty whole number of frames (size " +
                                    std::to_string(dst.size()) + ", channels " + std::to_string(channels_) + ")"};
    }

    // Promote any deferred boundary from the previous call (see
    // pendingLoopBoundary_ doc-string in audio_pipeline.h).
    restartedThisRead_   = pendingLoopBoundary_;
    pendingLoopBoundary_ = false;

    std::size_t filledSamples{0};
    bool rewoundSinceProgress{false};

    while (filledSamples < dst.size()) {
        const std::size_t samplesRead{source_.read(dst.subspan(filledSamples))};

        if (samplesRead > 0) {
            if (samplesRead % static_cast<std::size_t>(channels_) != 0) {
                throw std::logic_error{"AudioBlockReader::read: source returned a partial frame (" +
                                       std::to_string(samplesRead) + " samples, channels " + std::to_string(channels_) +
                                       ")"};
            }
            filledSamples += samplesRead;
            rewoundSinceProgress = false;
            if (source_.error()) {
                return AudioPipelineStatus::Error;
            }
            continue;
        }

        if (source_.error()) {
            return AudioPipelineStatus::Error;
        }

        // EOF in non-loop mode is terminal.
        if (loop_ == false) {
            if (filledSamples == 0) {
                return AudioPipelineStatus::End;
            }
            std::fill(dst.begin() + static_cast<std::ptrdiff_t>(filledSamples), dst.end(), 0.0F);
            return AudioPipelineStatus::Ok;
        }

        // Loop mode: rewind in place to keep the loop boundary gap-free.
        // A short crossfade smooths the seam when pre-rewind tail is
        // present in the buffer; on a block-aligned EOF there is no tail
        // and consumeLoopBoundary() lets the converter reset cleanly.
        if (rewoundSinceProgress) {
            // Source produced nothing after a rewind: it is empty.
            if (filledSamples == 0) {
                return AudioPipelineStatus::End;
            }
            // Tail of the block holds pre-rewind data plus a zero pad.
            // Defer the boundary signal so the next read() resets the
            // converter before processing post-rewind data: signalling
            // it now would discard the pre-rewind tail still queued in
            // the converter's filter state.
            pendingLoopBoundary_ = true;
            std::fill(dst.begin() + static_cast<std::ptrdiff_t>(filledSamples), dst.end(), 0.0F);
            return AudioPipelineStatus::Ok;
        }

        const std::size_t chCount{static_cast<std::size_t>(channels_)};
        const std::size_t blendFrames{std::min(kCrossfadeFrames, filledSamples / chCount)};

        if (source_.rewind() == false) {
            return AudioPipelineStatus::Error;
        }
        rewoundSinceProgress = true;

        if (blendFrames < 2) {
            // Block-aligned boundary, or pre-tail too short to blend.
            if (filledSamples == 0) {
                restartedThisRead_ = true;
            }
            continue;
        }

        // Pull blendFrames of post-rewind audio into scratch (handle short reads).
        const std::size_t blendSamples{blendFrames * chCount};
        std::size_t headFilled{0};
        while (headFilled < blendSamples) {
            const std::size_t got{
                source_.read(std::span<float>{crossfadeBuffer_.data() + headFilled, blendSamples - headFilled})};
            if (source_.error()) {
                return AudioPipelineStatus::Error;
            }
            if (got == 0) {
                break;
            }
            if (got % chCount != 0) {
                throw std::logic_error{"AudioBlockReader::read: source returned a partial frame during crossfade (" +
                                       std::to_string(got) + " samples, channels " + std::to_string(channels_) + ")"};
            }
            headFilled += got;
        }

        if (headFilled < blendSamples) {
            // File shorter than the crossfade window: append what we got
            // and let the outer loop hit EOF again.
            std::copy(crossfadeBuffer_.begin(),
                      crossfadeBuffer_.begin() + static_cast<std::ptrdiff_t>(headFilled),
                      dst.begin() + static_cast<std::ptrdiff_t>(filledSamples));
            filledSamples += headFilled;
            if (headFilled > 0) {
                rewoundSinceProgress = false;
            }
            continue;
        }

        // Linear crossfade in place over the trailing blendFrames of dst:
        // w goes 0 -> 1 per frame, so the seam matches both surrounding
        // sides without a discontinuity.
        const std::size_t blendStart{filledSamples - blendSamples};
        const float invDenom{1.0F / static_cast<float>(blendFrames - 1)};
        std::size_t dstIdx{blendStart};
        std::size_t srcIdx{0};
        for (std::size_t f{0}; f < blendFrames; ++f) {
            const float w{static_cast<float>(f) * invDenom};
            const float invW{1.0F - w};
            for (std::size_t c{0}; c < chCount; ++c) {
                dst[dstIdx] = dst[dstIdx] * invW + crossfadeBuffer_[srcIdx] * w;
                ++dstIdx;
                ++srcIdx;
            }
        }
        // Source is now at frame blendFrames of the rewound file; the
        // outer loop fills the remainder. Reset the empty-source guard
        // since post-head data was successfully consumed.
        rewoundSinceProgress = false;
    }

    return AudioPipelineStatus::Ok;
}

namespace {
    void downmixInterleavedToMono(std::span<const float> interleaved, int channels, std::span<float> mono) {
        if (channels <= 0) {
            throw std::invalid_argument{"downmixInterleavedToMono: channels must be positive, got " +
                                        std::to_string(channels)};
        }
        const auto chCount{static_cast<std::size_t>(channels)};
        if (interleaved.size() != mono.size() * chCount) {
            throw std::invalid_argument{"downmixInterleavedToMono: interleaved size (" +
                                        std::to_string(interleaved.size()) + ") must equal mono size (" +
                                        std::to_string(mono.size()) + ") * channels (" + std::to_string(channels) +
                                        ")"};
        }
        if (channels == 1) {
            std::copy(interleaved.begin(), interleaved.end(), mono.begin());
            return;
        }

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
    if (sourceFormat_.channels <= 0) {
        throw std::invalid_argument{"AudioPipeline: source channel count must be positive, got " +
                                    std::to_string(sourceFormat_.channels)};
    }
    // outputChannels_ is derived from sourceFormat_.channels (or fixed at 1
    // for the Mono mode) and is therefore positive whenever the source check
    // above passes - no separate guard required.

    rateConverters_.reserve(static_cast<std::size_t>(outputChannels_));
    for (int c{0}; c < outputChannels_; ++c) {
        rateConverters_.emplace_back(sourceFormat_.sampleRate, config_.targetSampleRate, config_.targetOutputFrames);
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
    interleavedInput_.assign(
        static_cast<std::size_t>(maxInputFrames_) * static_cast<std::size_t>(sourceFormat_.channels), 0.0F);
    channelInput_.assign(static_cast<std::size_t>(outputChannels_),
                         std::vector<float>(static_cast<std::size_t>(maxInputFrames_), 0.0F));
    channelOutput_.assign(static_cast<std::size_t>(outputChannels_),
                          std::vector<float>(static_cast<std::size_t>(outputFrames_), 0.0F));
}

AudioPipelineStatus AudioPipeline::read(std::span<float> out) {
    // Caller contract: output buffer must match the block size declared at
    // construction, and the pipeline must have been fully initialised.
    if (reader_ == std::nullopt) {
        throw std::logic_error{"AudioPipeline::read: pipeline is not initialised"};
    }
    if (out.size() != outputSamplesPerBlock()) {
        throw std::invalid_argument{"AudioPipeline::read: out.size() (" + std::to_string(out.size()) +
                                    ") must equal outputSamplesPerBlock() (" + std::to_string(outputSamplesPerBlock()) +
                                    ")"};
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
    // Internal invariant: AudioRateConverter must never request more than
    // maxInputFrames_ per call, which is exactly what interleavedInput_ is
    // sized for. A future contract drift would be a logic bug, not a
    // runtime IO failure - surface it as such.
    if (interleavedSize > interleavedInput_.size()) {
        throw std::logic_error{"AudioPipeline::read: rate converter requested " + std::to_string(interleavedSize) +
                               " interleaved samples, exceeding buffer capacity " +
                               std::to_string(interleavedInput_.size())};
    }

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
