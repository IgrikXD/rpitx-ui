/**
 * @file fmrds_processor.cpp
 * @brief FM broadcast MPX processor implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 26.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "fmrds_processor.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {
    /**
     * @brief Per-section pole-pair Q for an N-th order Butterworth cascade.
     *
     * Same derivation as nfm_processor.cpp: poles are evenly spaced on a
     * semicircle of radius omega_c in the LHP and the k-th pair sits at
     * angle theta_k = pi * (2k - 1) / (2N) measured from the negative real
     * axis. Q_k = 1 / (2 * cos(theta_k)) cascades to the correct N-th order
     * Butterworth response. Computed at run time because std::cos is not
     * constexpr in C++20.
     *
     * @param order   Target Butterworth order N (must be even and >= 2).
     * @param section Section index k in [1, N/2].
     * @return Pole-pair Q for the given section.
     */
    [[nodiscard]] float butterworthCascadeQ(int order, int section) {
        const float theta{std::numbers::pi_v<float> * static_cast<float>(2 * section - 1) /
                          static_cast<float>(2 * order)};
        return 1.0F / (2.0F * std::cos(theta));
    }
}  // namespace

float FmRdsProcessor::ChannelFilters::process(float x) {
    float s{hpf.process(x)};
    s = preEmphasis.process(s);
    for (auto& section: lpfChain) {
        s = section.process(s);
    }
    return s;
}

void FmRdsProcessor::validateConfig(const FmRdsConfig& config) {
    // Throw on broken contracts instead of asserting: this processor is
    // intended to be reusable, so critical invariants must surface as
    // runtime errors in release builds rather than vanish in NDEBUG.
    // Called from makeFilters() so validation runs before any Biquad is
    // constructed against config (a non-positive sample rate would
    // otherwise produce NaN coefficients before the constructor body
    // got a chance to throw).
    if (config.channels != 1 && config.channels != 2) {
        throw std::invalid_argument{"FmRdsProcessor: channels must be 1 or 2, got " + std::to_string(config.channels)};
    }
    if (config.mpxSampleRate <= 0) {
        throw std::invalid_argument{"FmRdsProcessor: mpxSampleRate must be positive, got " +
                                    std::to_string(config.mpxSampleRate)};
    }
}

FmRdsProcessor::ChannelFilters FmRdsProcessor::makeChannelFilters(const FmRdsConfig& config) {
    // Unchecked builder: callers must have already passed config through
    // validateConfig(). The single entry point that invokes this method
    // is makeFilters() below, which validates exactly once before fanning
    // out to per-channel construction.
    const auto fs{static_cast<float>(config.mpxSampleRate)};
    return ChannelFilters{
        .hpf         = Biquad::highPass(HPF_CUTOFF, fs),
        .preEmphasis = Biquad::preEmphasis(config.preEmphasisTau, fs),
        .lpfChain =
            {
                Biquad::lowPass(LPF_CUTOFF, fs, butterworthCascadeQ(LPF_ORDER, 1)),
                Biquad::lowPass(LPF_CUTOFF, fs, butterworthCascadeQ(LPF_ORDER, 2)),
            },
    };
}

std::array<FmRdsProcessor::ChannelFilters, 2> FmRdsProcessor::makeFilters(const FmRdsConfig& config) {
    // Single validation point for the whole filters_ array: validateConfig()
    // runs once here and the two per-channel builds below reuse the vetted
    // config without re-checking. Keeps the constructor's init list as a
    // single brace-enclosed factory call rather than two side-by-side
    // makeChannelFilters() invocations that would each re-validate.
    validateConfig(config);
    return {makeChannelFilters(config), makeChannelFilters(config)};
}

FmRdsProcessor::FmRdsProcessor(const FmRdsConfig& config)
    : channels_{config.channels}, peakDeviation_{config.peakDeviation}, filters_{makeFilters(config)} {
    // Runtime config validation runs inside makeFilters() above (exactly
    // once for the whole filters_ array), so by the time we get here
    // filters_ is guaranteed to have been built against a vetted config
    // and any contract violation has already surfaced as
    // std::invalid_argument before any Biquad was touched.

    // Silent failure mode: extra slots in std::array<Biquad, LPF_ORDER / 2>
    // would value-init to zero-coefficient biquads (inaudible output), not
    // real filter sections, so a mismatched LPF_ORDER / makeChannelFilters
    // pairing would not fail loudly. Mirror the nfm_processor invariant.
    static_assert(LPF_ORDER == 4,
                  "ChannelFilters::lpfChain has two Biquad entries; bumping LPF_ORDER "
                  "requires appending matching butterworthCascadeQ(order, k) calls "
                  "in makeChannelFilters() and updating this static_assert.");

    // Pre-compute the 19 kHz pilot and 38 kHz subcarrier sine tables.
    // 228 / 19 = 12 and 228 / 38 = 6 are exact integer ratios, so a small
    // LUT plus a phase counter replaces a per-sample sin/cos NCO.
    //
    // Sine (rather than cosine) form: both tables start at 0 with positive
    // slope, so at sample 0 the pilot and the subcarrier are simultaneously
    // at the rising zero crossing. That's the EN 50067 3.1.4.1 phase-lock
    // convention - a receiver recovering the pilot's rising zero crossing
    // demodulates the (L - R) subcarrier coherently. The convention also
    // matches PiFmRds upstream, so existing receivers known to lock against
    // that codebase keep working unchanged here.
    for (int k{0}; k < PILOT_19K_PERIOD; ++k) {
        pilotTable_[static_cast<std::size_t>(k)] =
            std::sin(2.0F * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(PILOT_19K_PERIOD));
    }
    for (int k{0}; k < CARRIER_38K_PERIOD; ++k) {
        carrierTable_[static_cast<std::size_t>(k)] =
            std::sin(2.0F * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(CARRIER_38K_PERIOD));
    }
}

RdsEncoder& FmRdsProcessor::encoder() {
    return rdsModulator_.encoder();
}

IqSample FmRdsProcessor::preprocessFrame(float l, float r) {
    const float lf{filters_[0].process(l)};

    // Mono path uses the scalar AGC overload directly. Feeding (lf, lf) into
    // the IqSample form would track sqrt(lf^2 + lf^2) = sqrt(2) * |lf|, which
    // drives the per-channel level to target / sqrt(2) ~= 0.566 instead of
    // target = 0.8 - an unintended ~3 dB attenuation that costs the mono
    // mode roughly 20 % of its deviation budget for no benefit.
    if (channels_ == 1) {
        const float agcd{agc_.process(lf)};
        const float clamped{std::clamp(agcd, -1.0F, 1.0F)};
        // Duplicate L into R so the downstream stereo MPX path can read
        // both buffers uniformly without a special-case branch.
        return {.i = clamped, .q = clamped};
    }

    const float rf{filters_[1].process(r)};
    const auto agcd{agc_.process(IqSample{.i = lf, .q = rf})};
    return {
        .i = std::clamp(agcd.i, -1.0F, 1.0F),
        .q = std::clamp(agcd.q, -1.0F, 1.0F),
    };
}

float FmRdsProcessor::buildMpxSample(float l, float r) {
    float mpx{0.0F};
    if (channels_ == 1) {
        // Mono: send the audio at the same combined-channel gain a stereo
        // (L + R) sum would reach, but skip the pilot and 38 kHz subcarrier
        // entirely. Receivers detect the missing pilot and drop into mono
        // demodulation, which is exactly what we want here - radiating an
        // unused pilot would just waste 10 % of the deviation budget.
        const float audio{2.0F * AUDIO_SUM_GAIN * l};
        const float rds{rdsModulator_.nextSample() * RDS_GAIN};
        mpx = audio + rds;
    } else {
        const float sum{AUDIO_SUM_GAIN * (l + r)};
        const float diff{AUDIO_SUM_GAIN * (l - r) * carrierTable_[static_cast<std::size_t>(carrierPhase_)]};
        const float pilot{PILOT_GAIN * pilotTable_[static_cast<std::size_t>(pilotPhase_)]};
        const float rds{rdsModulator_.nextSample() * RDS_GAIN};
        mpx = sum + diff + pilot + rds;
    }
    pilotPhase_ = (pilotPhase_ + 1) % PILOT_19K_PERIOD;
    // 12 / 6 == 2 phase ratio: the 38 kHz subcarrier must wrap exactly twice
    // per pilot cycle, so its phase counter advances at the same rate but
    // wraps modulo 6.
    carrierPhase_ = (carrierPhase_ + 1) % CARRIER_38K_PERIOD;

    return std::clamp(mpx, -1.0F, 1.0F) * peakDeviation_;
}

void FmRdsProcessor::process(std::span<const float> audioIn, std::span<float> mpxOut) {
    const auto frames{mpxOut.size()};
    const auto expectedAudio{frames * static_cast<std::size_t>(channels_)};
    if (audioIn.size() != expectedAudio) {
        throw std::invalid_argument{"FmRdsProcessor::process: audioIn.size() (" + std::to_string(audioIn.size()) +
                                    ") must equal mpxOut.size() * channels (" + std::to_string(expectedAudio) + ")"};
    }

    // Audio stage: HPF -> pre-emph -> LPF -> joint AGC, then MPX composition.
    // AudioPipeline already resampled the input to the MPX rate, so each
    // input frame maps directly to one output deviation sample.
    for (std::size_t i{0}; i < frames; ++i) {
        float l{};
        float r{};
        if (channels_ == 1) {
            l = audioIn[i];
            r = l;
        } else {
            const std::size_t frameOffset{i * 2};
            l = audioIn[frameOffset];
            r = audioIn[frameOffset + 1];
        }
        const auto frame{preprocessFrame(l, r)};
        mpxOut[i] = buildMpxSample(frame.i, frame.q);
    }
}
