/**
 * @file libsndfile_audio_source.h
 * @brief libsndfile-backed AudioSource implementation and factory functions.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "audio_source.h"

/**
 * @brief AudioSource backed by libsndfile.
 *
 * Owns the backend decoder handle and closes it in the destructor.
 * Backing-specific details stay hidden behind PIMPL while the read / rewind
 * path stays shared in one place. Non-copyable and non-movable (matches
 * AudioSource base).
 */
class LibsndfileAudioSource final : public AudioSource {
public:
    ~LibsndfileAudioSource() override;

    /**
     * @brief Return channel count and sample rate captured when the file was opened.
     */
    [[nodiscard]] AudioFormat format() const override;

    /**
     * @brief Return the human-readable format description captured at open time.
     */
    [[nodiscard]] std::string_view description() const override;

    /**
     * @brief Read interleaved float samples into dst.
     *
     * dst.size() must be a multiple of format().channels. Samples returned by
     * the backend are clamped to [-1, 1], and non-finite samples are replaced
     * with silence. A fatal read or decoder error makes error() sticky.
     *
     * @param dst Destination sample buffer.
     * @return Number of float samples written; 0 on EOF, prior error, or
     *         immediate read failure. A backend error after a partial read may
     *         return a positive count and set error().
     */
    [[nodiscard]] std::size_t read(std::span<float> dst) override;

    /**
     * @brief Seek back to the start of the stream.
     *
     * @return false if the source is not seekable or the backend seek fails.
     */
    [[nodiscard]] bool rewind() override;

    /**
     * @brief Report whether the backend marked this source as seekable at open time.
     */
    [[nodiscard]] bool seekable() const override;

    /**
     * @brief Report whether a fatal read or seek error has occurred.
     */
    [[nodiscard]] bool error() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit LibsndfileAudioSource(std::unique_ptr<Impl> impl);

    friend std::unique_ptr<AudioSource> makeFileAudioSource(const std::string& path);
};

/**
 * @brief Open a file-backed audio source via libsndfile.
 *
 * Accepts any format libsndfile is built with: WAV / AIFF / FLAC / OGG /
 * Opus / etc. Output samples are finite floats clamped to [-1, 1] regardless
 * of on-disk encoding (PCM_16, PCM_24, FLOAT, ...). Rewind support is taken
 * from libsndfile metadata, so regular files can loop while FIFO / device
 * paths fail loop validation.
 *
 * On failure prints a diagnostic to stderr and returns nullptr.
 *
 * @param path Path to the audio file.
 * @return Owning pointer to the source on success, nullptr on failure.
 */
[[nodiscard]] std::unique_ptr<AudioSource> makeFileAudioSource(const std::string& path);
