/**
 * @file libsndfile_audio_source.cpp
 * @brief libsndfile-backed AudioSource implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "libsndfile_audio_source.h"

#include <sndfile.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <utility>

namespace {
    [[nodiscard]] float sanitizeDecodedSample(float sample) noexcept {
        if (std::isfinite(sample) == false) {
            return 0.0F;
        }
        return std::clamp(sample, -1.0F, 1.0F);
    }

    /**
     * @brief Build a "MAJOR / SUBTYPE" description from a libsndfile SF_INFO.
     *
     * Two SFC_GET_FORMAT_INFO queries: one against the major-format mask
     * (WAV, FLAC, ...) and one against the subtype mask (PCM_16, PCM_24,
     * FLOAT, ...). Both are global queries (sf_command on a null handle),
     * so they can run before or after the SNDFILE handle is closed.
     *
     * @param info Source SF_INFO populated by sf_open / sf_open_virtual.
     * @return One-line description; "unknown" if both queries fail.
     */
    [[nodiscard]] std::string formatDescription(const SF_INFO& info) {
        SF_FORMAT_INFO mainInfo{};
        mainInfo.format = info.format & SF_FORMAT_TYPEMASK;
        const int mainOk{sf_command(nullptr, SFC_GET_FORMAT_INFO, &mainInfo, sizeof(mainInfo))};

        SF_FORMAT_INFO subInfo{};
        subInfo.format = info.format & SF_FORMAT_SUBMASK;
        const int subOk{sf_command(nullptr, SFC_GET_FORMAT_INFO, &subInfo, sizeof(subInfo))};

        std::string result;
        if (mainOk == 0 && mainInfo.name != nullptr) {
            result += mainInfo.name;
        }
        if (subOk == 0 && subInfo.name != nullptr) {
            if (result.empty() == false) {
                result += " / ";
            }
            result += subInfo.name;
        }
        if (result.empty()) {
            result = "unknown";
        }
        return result;
    }
}  // namespace

struct LibsndfileAudioSource::Impl {
    SNDFILE* handle{nullptr};
    AudioFormat format{};
    bool seekable{false};
    bool error{false};
    std::string description;

    Impl(SNDFILE* sourceHandle, const SF_INFO& sourceInfo, bool sourceSeekable, std::string sourceDescription)
        : handle{sourceHandle},
          format{.channels = sourceInfo.channels, .sampleRate = sourceInfo.samplerate},
          seekable{sourceSeekable},
          description{std::move(sourceDescription)} {
        assert(handle != nullptr);
    }

    ~Impl() {
        if (handle != nullptr) {
            sf_close(handle);
        }
    }

    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;
};

LibsndfileAudioSource::LibsndfileAudioSource(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {
    assert(impl_ != nullptr);
}

LibsndfileAudioSource::~LibsndfileAudioSource() = default;

AudioFormat LibsndfileAudioSource::format() const {
    return impl_->format;
}

std::string_view LibsndfileAudioSource::description() const {
    return impl_->description;
}

std::size_t LibsndfileAudioSource::read(std::span<float> dst) {
    if (impl_->error) {
        return 0;
    }

    const auto channels{static_cast<std::size_t>(impl_->format.channels)};
    // Caller is contractually responsible for sizing dst as a multiple of
    // channels; assert in debug builds so a stereo / mono mix-up is caught
    // immediately rather than after producing a half-frame at the tail.
    assert(channels > 0);
    assert(dst.size() % channels == 0);
    if (channels == 0 || dst.size() % channels != 0) {
        impl_->error = true;
        return 0;
    }

    const auto framesRequested{static_cast<sf_count_t>(dst.size() / channels)};
    const sf_count_t framesRead{sf_readf_float(impl_->handle, dst.data(), framesRequested)};
    if (framesRead < 0) {
        impl_->error = true;
        return 0;
    }

    // Short read at clean EOF is normal; only flag I/O errors. sf_error
    // returns SF_ERR_NO_ERROR (0) on a clean end-of-stream, non-zero on
    // an actual decoder / read failure.
    if (framesRead < framesRequested && sf_error(impl_->handle) != SF_ERR_NO_ERROR) {
        impl_->error = true;
    }

    const std::size_t samplesRead{static_cast<std::size_t>(framesRead) * channels};
    for (float& sample: dst.first(samplesRead)) {
        sample = sanitizeDecodedSample(sample);
    }
    return samplesRead;
}

bool LibsndfileAudioSource::rewind() {
    if (impl_->seekable == false) {
        return false;
    }
    if (sf_seek(impl_->handle, 0, SEEK_SET) < 0) {
        impl_->error = true;
        return false;
    }
    return true;
}

bool LibsndfileAudioSource::seekable() const {
    return impl_->seekable;
}

bool LibsndfileAudioSource::error() const {
    return impl_->error;
}

std::unique_ptr<AudioSource> makeFileAudioSource(const std::string& path) {
    SF_INFO info{};
    std::unique_ptr<SNDFILE, decltype(&sf_close)> handle{sf_open(path.c_str(), SFM_READ, &info), sf_close};
    if (handle == nullptr) {
        std::cerr << "[ERROR] Failed to open audio file '" << path << "': " << sf_strerror(nullptr) << std::endl;
        return nullptr;
    }

    auto impl{std::make_unique<LibsndfileAudioSource::Impl>(handle.get(), info, info.seekable != 0,
                                                            formatDescription(info))};
    handle.release();
    return std::unique_ptr<AudioSource>{new LibsndfileAudioSource{std::move(impl)}};
}
