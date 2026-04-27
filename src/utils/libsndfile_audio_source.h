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
 * Single concrete class shared by the file / stdin / raw factories below.
 * The variant is captured in the construction-time arguments (handle source,
 * seekable flag, description) rather than in the type, because libsndfile
 * provides a uniform read / seek API regardless of backing store - splitting
 * into three classes would just duplicate the read / rewind / close path.
 *
 * Owns the SNDFILE handle and closes it in the destructor. Non-copyable
 * (handle ownership is unique) and non-movable (matches AudioSource base).
 */
class LibsndfileAudioSource final: public AudioSource {
public:
    /**
     * @brief Construct from an open libsndfile handle.
     *
     * Used only by the factory functions below; normal callers should not
     * invoke this directly. Takes ownership of the handle.
     *
     * @param handle Open SNDFILE handle (must be non-null).
     * @param info Format information from sf_open / sf_open_virtual.
     * @param seekable Whether the backing supports rewind (true for files,
     *                 false for stdin).
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
 * Opus / etc. Output is always normalized float regardless of on-disk
 * encoding (PCM_16, PCM_24, FLOAT, ...). Backing is seekable, so rewind()
 * and --loop work.
 *
 * On failure prints a diagnostic to stderr and returns nullptr.
 *
 * @param path Path to the audio file.
 * @return Owning pointer to the source on success, nullptr on failure.
 */
[[nodiscard]] std::unique_ptr<AudioSource> makeFileAudioSource(const std::string& path);

/**
 * @brief Open a stdin-backed audio source via libsndfile virtual I/O.
 *
 * Wraps stdin with a libsndfile SF_VIRTUAL_IO whose seek / tell / get_filelen
 * callbacks all report "unsupported", forcing libsndfile into single-pass
 * streaming mode. Suitable for `cat file.wav | tool` style pipelines, but
 * not seekable - rewind() always fails, so --loop is incompatible with
 * stdin-mode and the caller should reject the combination at startup.
 *
 * Provided for completeness so future modules that need pipeline composition
 * can build against it; the pifmrds / piam / pinfm CLI uses makeFileAudioSource
 * exclusively because the cat-loop pattern injected RIFF headers mid-stream.
 *
 * On failure prints a diagnostic to stderr and returns nullptr.
 *
 * @return Owning pointer to the source on success, nullptr on failure.
 */
[[nodiscard]] std::unique_ptr<AudioSource> makeStdinAudioSource();

/**
 * @brief Open a headerless raw PCM file via libsndfile (16-bit signed PCM).
 *
 * For files that contain only audio samples with no container - the caller
 * must specify channel count and sample rate explicitly because there is
 * no header to read them from. Encoding is fixed at PCM_16; if other raw
 * encodings are needed in the future, this signature can grow a third
 * parameter without breaking the AudioSource interface.
 *
 * On failure prints a diagnostic to stderr and returns nullptr.
 *
 * @param path Path to the raw PCM file.
 * @param format Channel count and sample rate of the raw stream.
 * @return Owning pointer to the source on success, nullptr on failure.
 */
[[nodiscard]] std::unique_ptr<AudioSource> makeRawAudioSource(const std::string& path, AudioFormat format);
