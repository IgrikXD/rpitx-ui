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
