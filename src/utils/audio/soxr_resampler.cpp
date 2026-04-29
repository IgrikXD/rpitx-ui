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

#include <stdexcept>
#include <string>
#include <utility>

namespace {
    [[nodiscard]] constexpr unsigned long qualityRecipe(SoxrResampler::Quality q) noexcept {
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
    // Pass nullptr for the runtime spec so libsoxr applies its own defaults.
    // soxr_runtime_spec(1) hard-codes log2_min_dft_size = 10 (1024-point DFT
    // floor), which is larger than our per-block input on the 44.1 kHz ->
    // 48 kHz path (941 samples) and 44.1 kHz -> 228 kHz path (793 samples);
    // forcing that floor on undersized blocks crashed pinfm/pifmrds during
    // the very first soxr_process call. The library-default runtime spec
    // sizes its DFT lazily from the actual block size.

    soxr_error_t error{nullptr};
    handle_ = soxr_create(sourceRateHz, targetRateHz, 1, &error, &ioSpec, &qualitySpec, nullptr);
    if (error != nullptr || handle_ == nullptr) {
        const char* msg{error != nullptr ? error : "unknown error"};
        throw std::runtime_error{std::string{"SoxrResampler: soxr_create failed: "} + msg};
    }
}

SoxrResampler& SoxrResampler::operator=(SoxrResampler&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            soxr_delete(handle_);
        }
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

std::optional<SoxrProcessResult> SoxrResampler::process(std::span<const float> in, std::span<float> out) noexcept {
    if (handle_ == nullptr) {
        return std::nullopt;
    }

    std::size_t inputDone{0};
    std::size_t outputDone{0};

    // Hand soxr nullptr when a span is empty so we never pair a non-null
    // pointer with size 0; an empty input span is the documented EOS flush.
    const float* inPtr{in.empty() ? nullptr : in.data()};
    float* outPtr{out.empty() ? nullptr : out.data()};

    const soxr_error_t error{soxr_process(handle_, inPtr, in.size(), &inputDone, outPtr, out.size(), &outputDone)};
    if (error != nullptr) {
        return std::nullopt;
    }
    return SoxrProcessResult{.inputConsumed = inputDone, .outputProduced = outputDone};
}
