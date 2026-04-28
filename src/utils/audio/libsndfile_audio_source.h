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

#include <sndfile.h>

#include <memory>
#include <span>
#include <string>

#include "audio_source.h"

/**
 * @brief AudioSource backed by a libsndfile SNDFILE handle.
 *
 * Concrete class used by the file factory below. Backing-specific details
 * (seekable flag, description) are captured at construction time while the
 * read / rewind / close path stays shared in one place.
 *
 * Owns the SNDFILE handle and closes it in the destructor. Non-copyable
 * (handle ownership is unique) and non-movable (matches AudioSource base).
 */
class LibsndfileAudioSource final : public AudioSource {
public:
    /**
     * @brief Construct from an open libsndfile handle.
     *
     * Used only by the factory functions below; normal callers should not
     * invoke this directly. Takes ownership of the handle.
     *
     * @param handle Open SNDFILE handle (must be non-null).
     * @param info Format information from sf_open / sf_open_virtual.
     * @param seekable Whether the backing supports rewind.
     * @param description Human-readable format description for logging.
     */
    LibsndfileAudioSource(SNDFILE* handle, SF_INFO info, bool seekable, std::string description);

    ~LibsndfileAudioSource() override;

    [[nodiscard]] AudioFormat format() const override;
    [[nodiscard]] std::string description() const override;
    [[nodiscard]] std::size_t read(std::span<float> dst) override;
    [[nodiscard]] bool rewind() override;
    [[nodiscard]] bool seekable() const override;
    [[nodiscard]] bool error() const override;

private:
    SNDFILE* handle_;
    SF_INFO info_;
    bool seekable_;
    bool error_{false};
    std::string description_;
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
