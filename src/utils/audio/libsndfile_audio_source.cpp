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
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

    /**
     * @brief AudioSource backed by libsndfile.
     *
     * Hidden implementation returned through the AudioSource interface by
     * makeFileAudioSource().
     */
    class LibsndfileAudioSource final : public AudioSource {
    public:
        /**
         * @brief Construct from an open backend handle.
         *
         * Takes ownership of handle. The format, seekable flag, and
         * description are captured at construction time.
         *
         * @param handle      Open backend handle (must be non-null).
         * @param info        Format metadata populated by the backend.
         * @param seekable    Whether the source can seek back to the start.
         * @param description Human-readable format description.
         */
        LibsndfileAudioSource(SNDFILE* handle, const SF_INFO& info, bool seekable, std::string description)
            : handle_{handle},
              format_{.channels = info.channels, .sampleRate = info.samplerate},
              seekable_{seekable},
              description_{std::move(description)} {
            // makeFileAudioSource is the only construction site and rejects a
            // null handle before reaching this point, so no defensive runtime
            // check is needed here.
        }

        /**
         * @brief Close the owned backend handle.
         */
        ~LibsndfileAudioSource() override {
            if (handle_ != nullptr) {
                sf_close(handle_);
            }
        }

        /**
         * @brief Return channel count and sample rate captured at open time.
         */
        [[nodiscard]] AudioFormat format() const override {
            return format_;
        }

        /**
         * @brief Return the human-readable format description captured at open time.
         */
        [[nodiscard]] std::string_view description() const override {
            return description_;
        }

        /**
         * @brief Read interleaved float samples into dst.
         *
         * dst.size() must be a multiple of format().channels. Samples returned
         * by the backend are clamped to [-1, 1], and non-finite samples are
         * replaced with silence. A fatal read or decoder error makes error()
         * sticky.
         *
         * @param dst Destination sample buffer.
         * @return Number of float samples written; 0 on EOF, prior error, or
         *         immediate read failure. A backend error after a partial read
         *         may return a positive count and set error().
         */
        [[nodiscard]] std::size_t read(std::span<float> dst) override {
            if (error_) {
                return 0;
            }

            const auto channels{static_cast<std::size_t>(format_.channels)};
            // Caller is contractually responsible for sizing dst as a multiple of
            // channels; the explicit runtime check below catches a stereo / mono
            // mix-up at the tail of dst rather than producing a half-frame.
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

        /**
         * @brief Seek back to the start of the stream.
         *
         * @return false if the source is not seekable or the backend seek fails.
         */
        [[nodiscard]] bool rewind() override {
            if (seekable_ == false) {
                return false;
            }
            if (sf_seek(handle_, 0, SEEK_SET) < 0) {
                error_ = true;
                return false;
            }
            return true;
        }

        /**
         * @brief Report whether the backend marked this source as seekable at open time.
         */
        [[nodiscard]] bool seekable() const override {
            return seekable_;
        }

        /**
         * @brief Report whether a fatal read or seek error has occurred.
         */
        [[nodiscard]] bool error() const override {
            return error_;
        }

    private:
        SNDFILE* handle_;
        AudioFormat format_;
        bool seekable_;
        bool error_{false};
        std::string description_;
    };
}  // namespace

std::unique_ptr<AudioSource> makeFileAudioSource(const std::string& path) {
    SF_INFO info{};
    std::unique_ptr<SNDFILE, decltype(&sf_close)> handle{sf_open(path.c_str(), SFM_READ, &info), sf_close};
    if (handle == nullptr) {
        std::cerr << "[ERROR] Failed to open audio file '" << path << "': " << sf_strerror(nullptr) << std::endl;
        return nullptr;
    }

    auto source{
        std::make_unique<LibsndfileAudioSource>(handle.get(), info, info.seekable != 0, formatDescription(info))};
    handle.release();
    return source;
}

std::unique_ptr<AudioSource> makeStdinAudioSource() {
    SF_INFO info{};
    // close_desc = SF_FALSE: stdin is owned by the runtime, libsndfile must
    // not close fd 0 on sf_close. The factory still owns the SNDFILE handle
    // itself and releases it through sf_close as usual on destruction.
    std::unique_ptr<SNDFILE, decltype(&sf_close)> handle{sf_open_fd(fileno(stdin), SFM_READ, &info, SF_FALSE),
                                                         sf_close};
    if (handle == nullptr) {
        std::cerr << "[ERROR] Failed to open stdin as audio source: " << sf_strerror(nullptr) << std::endl;
        return nullptr;
    }

    // Force seekable=false even when libsndfile reports otherwise (e.g. when
    // stdin happens to be a redirected regular file). Pipe / FIFO inputs are
    // the common case for --stdin, and treating the source uniformly as a
    // stream lets validateLoopSupport() reject --loop with a single,
    // consistent diagnostic instead of one that depends on how stdin was
    // wired up at the shell.
    auto source{
        std::make_unique<LibsndfileAudioSource>(handle.get(), info, false, "stdin / " + formatDescription(info))};
    handle.release();
    return source;
}

std::unique_ptr<AudioSource> makeAudioSource(bool useStdin, const std::string& path) {
    if (useStdin) {
        return makeStdinAudioSource();
    }
    return makeFileAudioSource(path);
}
