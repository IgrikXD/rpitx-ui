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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <utility>

namespace {
    [[nodiscard]] float sanitizeDecodedSample(float sample) {
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

LibsndfileAudioSource::LibsndfileAudioSource(SNDFILE* handle, SF_INFO info, bool seekable, std::string description)
    : handle_{handle}, info_{info}, seekable_{seekable}, description_{std::move(description)} {
    assert(handle != nullptr);
}

LibsndfileAudioSource::~LibsndfileAudioSource() {
    sf_close(handle_);
}

AudioFormat LibsndfileAudioSource::format() const {
    return AudioFormat{
        .channels   = info_.channels,
        .sampleRate = static_cast<int>(info_.samplerate),
    };
}

std::string LibsndfileAudioSource::description() const {
    return description_;
}

std::size_t LibsndfileAudioSource::read(std::span<float> dst) {
    if (error_) {
        return 0;
    }

    const auto channels{static_cast<std::size_t>(info_.channels)};
    // Caller is contractually responsible for sizing dst as a multiple of
    // channels; assert in debug builds so a stereo / mono mix-up is caught
    // immediately rather than after producing a half-frame at the tail.
    assert(channels > 0);
    assert(dst.size() % channels == 0);
    if (channels == 0 || dst.size() % channels != 0) {
        error_ = true;
        return 0;
    }

    const auto framesRequested{static_cast<sf_count_t>(dst.size() / channels)};
    const sf_count_t framesRead{sf_readf_float(handle_, dst.data(), framesRequested)};
    if (framesRead < 0) {
        error_ = true;
        return 0;
    }

    // Short read at clean EOF is normal; only flag I/O errors. sf_error
    // returns SF_ERR_NO_ERROR (0) on a clean end-of-stream, non-zero on
    // an actual decoder / read failure.
    if (framesRead < framesRequested && sf_error(handle_) != SF_ERR_NO_ERROR) {
        error_ = true;
    }

    const std::size_t samplesRead{static_cast<std::size_t>(framesRead) * channels};
    for (float& sample: dst.first(samplesRead)) {
        sample = sanitizeDecodedSample(sample);
    }
    return samplesRead;
}

bool LibsndfileAudioSource::rewind() {
    if (seekable_ == false) {
        return false;
    }
    const bool ok{sf_seek(handle_, 0, SEEK_SET) >= 0};
    if (ok == false) {
        error_ = true;
    }
    return ok;
}

bool LibsndfileAudioSource::seekable() const {
    return seekable_;
}

bool LibsndfileAudioSource::error() const {
    return error_;
}

std::unique_ptr<AudioSource> makeFileAudioSource(const std::string& path) {
    SF_INFO info{};
    SNDFILE* handle{sf_open(path.c_str(), SFM_READ, &info)};
    if (handle == nullptr) {
        std::cerr << "[ERROR] Failed to open audio file '" << path << "': " << sf_strerror(nullptr) << std::endl;
        return nullptr;
    }
    return std::make_unique<LibsndfileAudioSource>(handle, info, info.seekable != 0, formatDescription(info));
}
