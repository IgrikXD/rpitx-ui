/**
 * @file polyphase_resampler.cpp
 * @brief Polyphase FIR resampler implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 25.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "polyphase_resampler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace {
    struct PolyphaseRatio {
        int L;
        int M;
    };

    [[nodiscard]] constexpr PolyphaseRatio computePolyphaseRatio(int sourceRate, int targetRate) {
        const int g{std::gcd(sourceRate, targetRate)};
        return PolyphaseRatio{.L = targetRate / g, .M = sourceRate / g};
    }

    [[nodiscard]] int alignedInputForOutput(int targetOutput, int L, int M) {
        // ceil(targetOutput * M / L), then round up to the next multiple of M.
        const long long approxIn{(static_cast<long long>(targetOutput) * static_cast<long long>(M) +
                                  static_cast<long long>(L) - 1LL) /
                                 static_cast<long long>(L)};
        const long long alignedIn{((approxIn + static_cast<long long>(M) - 1LL) / static_cast<long long>(M)) *
                                  static_cast<long long>(M)};
        if (alignedIn > static_cast<long long>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument{"AudioRateConverter input frame count overflows int"};
        }
        return static_cast<int>(alignedIn);
    }

    [[nodiscard]] constexpr float safeResamplerCutoff(int sourceRate, int targetRate, float maxCutoffHz) {
        const float minRate{static_cast<float>(std::min(sourceRate, targetRate))};
        return std::min(maxCutoffHz, 0.45F * minRate);
    }

    void validateResamplerArgs(int interpL, int decimM, int tapsPerPhase, float cutoffHz, float sampleRateIn) {
        if (interpL < 1 || decimM < 1 || tapsPerPhase < 1 || cutoffHz <= 0.0F || sampleRateIn <= 0.0F ||
            std::isfinite(cutoffHz) == false || std::isfinite(sampleRateIn) == false) {
            throw std::invalid_argument{"Invalid PolyphaseResampler parameters"};
        }
        if (interpL > std::numeric_limits<int>::max() / tapsPerPhase) {
            throw std::invalid_argument{"PolyphaseResampler tap count overflows int"};
        }
    }

    void validateRateConverterArgs(int sourceRate, int targetRate, int targetOutputFrames, int tapsPerPhase,
                                   float maxCutoffHz) {
        if (sourceRate < 1 || targetRate < 1 || targetOutputFrames < 1 || tapsPerPhase < 1 || maxCutoffHz <= 0.0F ||
            std::isfinite(maxCutoffHz) == false) {
            throw std::invalid_argument{"Invalid AudioRateConverter parameters"};
        }
    }

    /**
     * @brief Evaluate the prototype filter h[k] at index k.
     *
     * The prototype is a Hamming-windowed sinc centred at (totalTaps - 1) / 2
     * (an integer for odd totalTaps, half-integer for even - both work because
     * the sinc is evaluated at the corresponding offset). Per-phase unity DC
     * gain normalisation happens after polyphase decomposition.
     *
     * @param k             Tap index, 0 <= k < totalTaps.
     * @param totalTaps     Total prototype filter length (== L * tapsPerPhase).
     * @param fcNormalised  Cutoff frequency normalised to the virtual rate
     *                      (cycles per virtual sample); must be in (0, 0.5).
     * @return Filter coefficient before polyphase decomposition.
     */
    [[nodiscard]] float evaluateProto(int k, int totalTaps, float fcNormalised) {
        const float center{static_cast<float>(totalTaps - 1) / 2.0f};
        const float m{static_cast<float>(k) - center};

        // Sinc at the cutoff: 2 * fc * sinc(2 * fc * m). The m == 0 branch
        // sidesteps the 0/0 form analytically (limit is 2 * fc).
        float h{};
        if (m == 0.0f) {
            h = 2.0f * fcNormalised;
        } else {
            const float arg{2.0f * std::numbers::pi_v<float> * fcNormalised * m};
            h = std::sin(arg) / (std::numbers::pi_v<float> * m);
        }

        // Hamming window: 0.54 - 0.46 * cos(2 pi k / (N - 1)). It gives
        // predictable stop-band attenuation without the ripple of a rectangular
        // truncation, which is enough for the shared AM / FM audio guards.
        const float window{0.54f - 0.46f * std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(k) /
                                                    static_cast<float>(totalTaps - 1))};
        return h * window;
    }
}  // namespace

PolyphaseResampler::PolyphaseResampler(int interpL, int decimM, int tapsPerPhase, float cutoffHz, float sampleRateIn)
    : L_{0}, M_{0}, tapsPerPhase_{0} {
    validateResamplerArgs(interpL, decimM, tapsPerPhase, cutoffHz, sampleRateIn);

    L_            = interpL;
    M_            = decimM;
    tapsPerPhase_ = tapsPerPhase;
    coefs_.assign(static_cast<std::size_t>(L_) * static_cast<std::size_t>(tapsPerPhase_), 0.0F);
    delay_.assign(static_cast<std::size_t>(tapsPerPhase_), 0.0F);

    const int totalTaps{L_ * tapsPerPhase_};
    // Cutoff is specified at the input rate but the prototype operates at the
    // virtual L*Fs rate, so divide by L to get cycles per virtual sample.
    const float fcNormalised{cutoffHz / (static_cast<float>(L_) * sampleRateIn)};
    if (fcNormalised <= 0.0F || fcNormalised >= 0.5F || std::isfinite(fcNormalised) == false) {
        throw std::invalid_argument{"Invalid PolyphaseResampler cutoff"};
    }

    // Decompose the prototype into L polyphase sub-filters. Each sub-filter
    // gets the +L scaling that compensates for the implicit zero-stuffing in
    // the polyphase decomposition (without it, every output is divided by L).
    // Then normalise each row independently so a DC input keeps unity gain
    // even when the finite window leaves the raw row sum below 1.0.
    //
    // Layout note: coefs_[phase][tap] = h_proto[tap * L + phase] is the
    // standard polyphase mapping. The +L scaling is folded in here so the
    // run-time inner product needs no per-output division.
    for (int phase{0}; phase < L_; ++phase) {
        float rowSum{0.0F};
        for (int tap{0}; tap < tapsPerPhase_; ++tap) {
            const int k{tap * L_ + phase};
            const std::size_t coefIndex{static_cast<std::size_t>(phase) * static_cast<std::size_t>(tapsPerPhase_) +
                                        static_cast<std::size_t>(tap)};
            coefs_[coefIndex] = evaluateProto(k, totalTaps, fcNormalised) * static_cast<float>(L_);
            rowSum += coefs_[coefIndex];
        }

        if (std::abs(rowSum) <= 1e-12F || std::isfinite(rowSum) == false) {
            throw std::invalid_argument{"Invalid PolyphaseResampler coefficient sum"};
        }
        for (int tap{0}; tap < tapsPerPhase_; ++tap) {
            const std::size_t coefIndex{static_cast<std::size_t>(phase) * static_cast<std::size_t>(tapsPerPhase_) +
                                        static_cast<std::size_t>(tap)};
            coefs_[coefIndex] /= rowSum;
        }
    }
}

std::optional<std::size_t> PolyphaseResampler::outputSize(std::size_t inSize) const {
    if (inSize % static_cast<std::size_t>(M_) != 0) {
        return std::nullopt;
    }
    if (inSize > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(L_)) {
        return std::nullopt;
    }
    return inSize * static_cast<std::size_t>(L_) / static_cast<std::size_t>(M_);
}

void PolyphaseResampler::pushDelay(float sample) {
    // Shift newest-first: delay_[0] is the most recent input, the rest slide
    // back by one. tapsPerPhase is small (typically 32) so the linear shift
    // is comparable to a circular buffer, with much simpler indexing in the
    // hot convolution loop.
    for (int i{tapsPerPhase_ - 1}; i > 0; --i) {
        delay_[static_cast<std::size_t>(i)] = delay_[static_cast<std::size_t>(i - 1)];
    }
    delay_[0] = sample;
}

float PolyphaseResampler::convolveAtPhase(int phase) const {
    const float* row{&coefs_[static_cast<std::size_t>(phase) * static_cast<std::size_t>(tapsPerPhase_)]};
    float acc{0.0f};
    for (int t{0}; t < tapsPerPhase_; ++t) {
        acc += row[t] * delay_[static_cast<std::size_t>(t)];
    }
    return acc;
}

bool PolyphaseResampler::resample(std::span<const float> in, std::span<float> out) {
    const auto expectedOut{outputSize(in.size())};
    if (expectedOut == std::nullopt || out.size() != expectedOut.value()) {
        return false;
    }

    std::size_t outIdx{0};
    for (std::size_t i{0}; i < in.size(); ++i) {
        pushDelay(in[i]);

        // The smallest phase within this input that falls on a kept (mod-M)
        // virtual sample is the additive inverse of virtualPhase_ (mod M):
        // (phase + virtualPhase_) mod M == 0  <=>  phase mod M == (M - virtualPhase_) mod M.
        // From there we step by M, producing 4 or 5 outputs per input for the
        // 19/4 case, 19 outputs in total per 4 inputs.
        const int target{(M_ - virtualPhase_) % M_};
        for (int phase{target}; phase < L_; phase += M_) {
            out[outIdx++] = convolveAtPhase(phase);
        }
        virtualPhase_ = (virtualPhase_ + L_) % M_;
    }

    assert(outIdx == out.size());
    return true;
}

AudioRateConverter::AudioRateConverter(int sourceRate, int targetRate, int targetOutputFrames, int tapsPerPhase,
                                       float maxCutoffHz)
    : inputFrames_{0}, outputFrames_{0} {
    validateRateConverterArgs(sourceRate, targetRate, targetOutputFrames, tapsPerPhase, maxCutoffHz);

    if (sourceRate == targetRate) {
        // Passthrough: identical rates, block size is verbatim the requested
        // output target. resampler_ stays nullopt; process() will memcpy.
        inputFrames_  = targetOutputFrames;
        outputFrames_ = targetOutputFrames;
        return;
    }

    const auto ratio{computePolyphaseRatio(sourceRate, targetRate)};
    inputFrames_  = alignedInputForOutput(targetOutputFrames, ratio.L, ratio.M);
    if (inputFrames_ > std::numeric_limits<int>::max() / ratio.L) {
        throw std::invalid_argument{"AudioRateConverter output frame count overflows int"};
    }
    outputFrames_ = inputFrames_ * ratio.L / ratio.M;
    const float cutoff{safeResamplerCutoff(sourceRate, targetRate, maxCutoffHz)};
    resampler_.emplace(ratio.L, ratio.M, tapsPerPhase, cutoff, static_cast<float>(sourceRate));
}

int AudioRateConverter::inputFrames() const {
    return inputFrames_;
}

int AudioRateConverter::outputFrames() const {
    return outputFrames_;
}

bool AudioRateConverter::process(std::span<const float> in, std::span<float> out) {
    if (in.size() != static_cast<std::size_t>(inputFrames_) || out.size() != static_cast<std::size_t>(outputFrames_)) {
        return false;
    }

    if (resampler_ == std::nullopt) {
        // Passthrough: input and output spans have identical size by ctor
        // invariant; std::copy is the canonical zero-overhead memcpy here.
        std::copy(in.begin(), in.end(), out.begin());
        return true;
    }
    return resampler_.value().resample(in, out);
}
