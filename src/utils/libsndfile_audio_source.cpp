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

#include <cassert>
#include <cstdio>
#include <iostream>
#include <utility>

namespace {
    /**
     * @brief libsndfile SF_VIRTUAL_IO callbacks for stdin-backed reads.
     *
     * All seek-related callbacks return -1 (unsupported), which forces
     * libsndfile into single-pass streaming mode. This is sufficient for
     * standard WAV / AIFF read-only access where the header sits at the
     * start of the stream and the data chunk runs to end-of-stream.
     */
    sf_count_t stdinGetFilelen([[maybe_unused]] void* userData) {
        return -1;
    }
    sf_count_t stdinSeek([[maybe_unused]] sf_count_t offset, [[maybe_unused]] int whence,
                         [[maybe_unused]] void* userData) {
        return -1;
    }
    sf_count_t stdinRead(void* ptr, sf_count_t count, [[maybe_unused]] void* userData) {
        return static_cast<sf_count_t>(std::fread(ptr, 1, static_cast<std::size_t>(count), stdin));
    }
    sf_count_t stdinWrite([[maybe_unused]] const void* ptr, [[maybe_unused]] sf_count_t count,
                          [[maybe_unused]] void* userData) {
        return 0;
    }
    sf_count_t stdinTell([[maybe_unused]] void* userData) {
        return -1;
    }

    /**
     * @brief Aggregate of the stdin VIO callbacks; passed to sf_open_virtual.
     *
     * Positional initialization (not designated) for portability across
     * libsndfile versions that may reorder fields between releases.
     */
    SF_VIRTUAL_IO STDIN_VIRTUAL_IO{
        stdinGetFilelen,
        stdinSeek,
        stdinRead,
        stdinWrite,
        stdinTell,
    };

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
    : handle_{handle},
      info_{info},
      seekable_{seekable},
      description_{std::move(description)} {
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
    const auto channels{static_cast<std::size_t>(info_.channels)};
    // Caller is contractually responsible for sizing dst as a multiple of
    // channels; assert in debug builds so a stereo / mono mix-up is caught
    // immediately rather than after producing a half-frame at the tail.
    assert(channels > 0);
    assert(dst.size() % channels == 0);

    const auto framesRequested{static_cast<sf_count_t>(dst.size() / channels)};
    const sf_count_t framesRead{sf_readf_float(handle_, dst.data(), framesRequested)};

    // Short read at clean EOF is normal; only flag I/O errors. sf_error
    // returns SF_ERR_NO_ERROR (0) on a clean end-of-stream, non-zero on
    // an actual decoder / read failure.
    if (framesRead < framesRequested && sf_error(handle_) != SF_ERR_NO_ERROR) {
        error_ = true;
    }
    return static_cast<std::size_t>(framesRead) * channels;
}

bool LibsndfileAudioSource::rewind() {
    if (seekable_ == false) {
        return false;
    }
    return sf_seek(handle_, 0, SEEK_SET) >= 0;
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
    return std::make_unique<LibsndfileAudioSource>(handle, info, /*seekable=*/true, formatDescription(info));
}

std::unique_ptr<AudioSource> makeStdinAudioSource() {
    SF_INFO info{};
    SNDFILE* handle{sf_open_virtual(&STDIN_VIRTUAL_IO, SFM_READ, &info, nullptr)};
    if (handle == nullptr) {
        std::cerr << "[ERROR] Failed to open stdin as audio source: " << sf_strerror(nullptr) << std::endl;
        return nullptr;
    }
    return std::make_unique<LibsndfileAudioSource>(handle, info, /*seekable=*/false, formatDescription(info));
}

std::unique_ptr<AudioSource> makeRawAudioSource(const std::string& path, AudioFormat format) {
    SF_INFO info{};
    info.samplerate = format.sampleRate;
    info.channels   = format.channels;
    // SF_FORMAT_RAW disables container parsing; SF_FORMAT_PCM_16 fixes the
    // sample encoding to signed 16-bit. Other raw encodings can be added
    // later by extending the AudioFormat / factory signature - the
    // AudioSource interface itself is encoding-agnostic.
    info.format = SF_FORMAT_RAW | SF_FORMAT_PCM_16;

    SNDFILE* handle{sf_open(path.c_str(), SFM_READ, &info)};
    if (handle == nullptr) {
        std::cerr << "[ERROR] Failed to open raw audio file '" << path << "': " << sf_strerror(nullptr) << std::endl;
        return nullptr;
    }
    return std::make_unique<LibsndfileAudioSource>(handle, info, /*seekable=*/true, formatDescription(info));
}
