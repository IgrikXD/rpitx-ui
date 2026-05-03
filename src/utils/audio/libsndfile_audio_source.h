/**
 * @file libsndfile_audio_source.h
 * @brief libsndfile-backed AudioSource factory function.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <memory>
#include <string>

#include "audio_source.h"

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

/**
 * @brief Open stdin as an audio source via libsndfile.
 *
 * Wraps `fileno(stdin)` through `sf_open_fd` with `close_desc=SF_FALSE` so
 * libsndfile never closes the inherited stdin file descriptor on us. The
 * resulting source is always reported as non-seekable, regardless of what
 * libsndfile's metadata claims about the underlying fd: a pipe or terminal
 * cannot be rewound, and even a redirected regular file is treated as a
 * stream so that `--loop` consistently fails fast through validateLoopSupport()
 * instead of intermittently working only when stdin happens to be a file.
 *
 * Practical formats: WAV / AIFF / FLAC headers can be parsed sequentially,
 * so those usually work over a pipe. Containers that require seeking back
 * (e.g. some Ogg / Opus muxes) may fail at open time with a libsndfile
 * diagnostic; rerun with `--audio <path>` in that case.
 *
 * On failure prints a diagnostic to stderr and returns nullptr.
 *
 * @return Owning pointer to the source on success, nullptr on failure.
 */
[[nodiscard]] std::unique_ptr<AudioSource> makeStdinAudioSource();

/**
 * @brief Open either stdin or a file as an audio source, based on a flag.
 *
 * Thin dispatcher over makeStdinAudioSource() and makeFileAudioSource() so
 * each transmitter binary can resolve its `--stdin` / `--audio` choice in a
 * single call instead of repeating the same if/else at every call site.
 *
 * @param useStdin When true, open stdin and ignore path.
 * @param path     Audio file path (used only when useStdin is false).
 * @return Owning pointer to the source on success, nullptr on failure.
 */
[[nodiscard]] std::unique_ptr<AudioSource> makeAudioSource(bool useStdin, const std::string& path);
