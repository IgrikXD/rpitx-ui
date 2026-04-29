/**
 * @file soxr_resampler.cpp
 * @brief libsoxr RAII wrapper implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 29.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "soxr_resampler.h"

#include <soxr.h>

#include <stdexcept>
#include <string>

namespace {
    [[nodiscard]] unsigned long qualityRecipe(SoxrResampler::Quality q) {
        switch (q) {
            case SoxrResampler::Quality::Quick:
                return SOXR_QQ;
            case SoxrResampler::Quality::Low:
                return SOXR_LQ;
            case SoxrResampler::Quality::Medium:
                return SOXR_MQ;
            case SoxrResampler::Quality::High:
                return SOXR_HQ;
            case SoxrResampler::Quality::VeryHigh:
                return SOXR_VHQ;
        }
        return SOXR_HQ;
    }
}  // namespace

SoxrResampler::SoxrResampler(int sourceRateHz, int targetRateHz, Quality quality) : handle_{nullptr} {
    if (sourceRateHz <= 0 || targetRateHz <= 0) {
        throw std::invalid_argument{"SoxrResampler: rates must be positive"};
    }

    // Float32 interleaved both ways. With num_channels=1 interleaved and
    // planar are bit-identical, so this is the natural single-stream layout.
    const soxr_io_spec_t ioSpec{soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I)};
    const soxr_quality_spec_t qualitySpec{soxr_quality_spec(qualityRecipe(quality), 0)};
    // Force single-thread mode: the per-channel block sizes used by the audio
    // pipeline are far below soxr's threading break-even, and skipping the
    // thread pool keeps pthread out of the binary's runtime cost.
    const soxr_runtime_spec_t runtimeSpec{soxr_runtime_spec(1)};

    soxr_error_t error{nullptr};
    handle_ = soxr_create(static_cast<double>(sourceRateHz),
                          static_cast<double>(targetRateHz),
                          1,
                          &error,
                          &ioSpec,
                          &qualitySpec,
                          &runtimeSpec);
    if (error != nullptr || handle_ == nullptr) {
        const char* msg{error != nullptr ? error : "unknown error"};
        throw std::runtime_error{std::string{"SoxrResampler: soxr_create failed: "} + msg};
    }
}

SoxrResampler::~SoxrResampler() {
    if (handle_ != nullptr) {
        soxr_delete(static_cast<soxr_t>(handle_));
    }
}

SoxrResampler::SoxrResampler(SoxrResampler&& other) noexcept : handle_{other.handle_} {
    other.handle_ = nullptr;
}

SoxrResampler& SoxrResampler::operator=(SoxrResampler&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            soxr_delete(static_cast<soxr_t>(handle_));
        }
        handle_       = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

std::optional<SoxrProcessResult> SoxrResampler::process(std::span<const float> in, std::span<float> out) {
    if (handle_ == nullptr) {
        return std::nullopt;
    }

    std::size_t inputDone{0};
    std::size_t outputDone{0};

    // soxr accepts a null/0-length input as the canonical "flush" form -
    // pass nullptr explicitly when the span is empty so we never hand soxr
    // a non-null pointer paired with size 0.
    const float* inPtr{in.empty() ? nullptr : in.data()};
    float* outPtr{out.empty() ? nullptr : out.data()};

    const soxr_error_t error{soxr_process(static_cast<soxr_t>(handle_),
                                          inPtr,
                                          in.size(),
                                          &inputDone,
                                          outPtr,
                                          out.size(),
                                          &outputDone)};
    if (error != nullptr) {
        return std::nullopt;
    }
    return SoxrProcessResult{.inputConsumed = inputDone, .outputProduced = outputDone};
}
