/**
 * @file tone_sampler.h
 * @brief Conversion of tones into the frequency samples of the DMA engine.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 13.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "sstv_mode.h"

namespace pisstv {
    /**
     * @brief Turns tones into frequency samples, one per sample period of the DMA engine.
     *
     * A tone rarely lasts a whole number of samples. The fraction of a sample a
     * tone leaves over is carried into the next one, so every tone ends within
     * half a sample of its position in the sample stream however many tones
     * precede it.
     *
     * @code
     * ToneSampler sampler{48'000};
     * sampler.append(tones, samples);
     * @endcode
     */
    class ToneSampler {
    public:
        /**
         * @brief Construct a sampler for the given sample rate.
         * @param sampleRate Sample rate of the DMA engine in Hz.
         */
        explicit ToneSampler(std::uint32_t sampleRate) noexcept
            : samplesPerMs_{static_cast<double>(sampleRate) / 1000.0} {
        }

        /**
         * @brief Append the samples of tones to samples: the frequency of each tone for its duration.
         */
        void append(std::span<const Tone> tones, std::vector<float>& samples) {
            for (const Tone& tone: tones) {
                owedSamples_ += tone.durationMs * samplesPerMs_;
                const auto count{static_cast<std::size_t>(std::floor(owedSamples_ + 0.5))};
                samples.insert(samples.end(), count, tone.frequency);
                owedSamples_ -= static_cast<double>(count);
            }
        }

    private:
        double samplesPerMs_;
        double owedSamples_{0.0};  ///< Duration of the tones so far that is not yet covered by samples, within +-0.5.
    };
}  // namespace pisstv
