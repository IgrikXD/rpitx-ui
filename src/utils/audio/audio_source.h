/**
 * @file audio_source.h
 * @brief Abstract streaming audio source interface for rpitx-ui transmitters.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstddef>
#include <span>
#include <string_view>

/**
 * @brief Audio metadata exposed to consumers of an AudioSource.
 *
 * Only the two fields callers act on are surfaced: channels drives buffer
 * sizing and mono / stereo branching, sampleRate drives resampler
 * configuration. Bit depth and subtype (PCM_16, PCM_24, FLOAT, ...) are
 * diagnostic-only and appear in AudioSource::description() - read() always
 * yields normalized float in [-1.0, 1.0] regardless of on-disk encoding.
 */
struct AudioFormat {
    int channels;    ///< Channel count (1 = mono, 2 = stereo, ...).
    int sampleRate;  ///< Audio sample rate in Hz.
};

/**
 * @brief Streaming pull-mode audio source producing normalized float samples.
 *
 * Concrete implementations wrap a backing decoder (libsndfile for files).
 * Output is always interleaved float in [-1.0, 1.0] regardless of on-disk
 * format so consumers do not have to branch on int16 / int24 / float / etc.
 *
 * Loop semantics live in the consumer, not in the source: when read() returns
 * 0 the caller distinguishes EOF from a fatal error via error() and decides
 * whether to call rewind() or stop. Sources stay policy-free; rewind() returns
 * false on non-seekable backings, which the caller can pre-check via
 * seekable() to fail fast on --loop over stdin / FIFO inputs.
 */
class AudioSource {
public:
    AudioSource(const AudioSource&)            = delete;
    AudioSource& operator=(const AudioSource&) = delete;
    AudioSource(AudioSource&&)                 = delete;
    AudioSource& operator=(AudioSource&&)      = delete;

    virtual ~AudioSource() = default;

    /**
     * @brief Audio metadata, available immediately after construction.
     *
     * @return Channel count and sample rate of the underlying stream.
     */
    [[nodiscard]] virtual AudioFormat format() const = 0;

    /**
     * @brief Human-readable description of the source for startup logging.
     *
     * Format and content are implementation-defined; intended only for
     * single-line diagnostic banners (e.g. "WAV / Signed 24 bit PCM").
     * Consumers must not parse this string - it has no stable schema.
     *
     * The returned view stays valid for the lifetime of the AudioSource.
     *
     * @return One-line description suitable for console output.
     */
    [[nodiscard]] virtual std::string_view description() const = 0;

    /**
     * @brief Read up to dst.size() interleaved float samples into dst.
     *
     * For stereo, samples are interleaved L, R, L, R, ... and dst.size()
     * must be a non-empty whole multiple of format().channels. The caller
     * derives frame count via dst.size() / format().channels.
     *
     * Returns the number of float samples actually written. A return value
     * less than dst.size() indicates either a clean end-of-stream or a fatal
     * source error; distinguish via error(). A fatal error may be reported
     * after a positive short read if the backend returned partial data before
     * surfacing the failure. Output samples are finite and clamped to [-1, 1].
     *
     * Implementations must throw std::invalid_argument when dst is empty or
     * its size is not a whole multiple of channels: a contract violation is
     * a programmer error and silently degrading to a sticky error / zero
     * return would mask it.
     *
     * @param dst Destination buffer; size must be a non-empty multiple of channels.
     * @return Number of float samples written (0 on EOF or sticky error).
     * @throws std::invalid_argument when dst is empty or not aligned to channels.
     */
    [[nodiscard]] virtual std::size_t read(std::span<float> dst) = 0;

    /**
     * @brief Reset the read position to the start of the stream.
     *
     * Returns true on success; false if the source is not seekable or the
     * underlying seek fails. Callers using --loop should validate
     * seekable() at startup so a non-seekable source rejects --loop with a
     * clear diagnostic instead of failing only after the first EOF.
     *
     * @return true on success, false otherwise.
     */
    [[nodiscard]] virtual bool rewind() = 0;

    /**
     * @brief Whether the underlying stream supports rewind().
     *
     * Regular file-backed sources are usually seekable; FIFO / device paths
     * are not. Used by callers to fail fast at startup when --loop is
     * requested over a non-seekable backing.
     *
     * @return true if rewind() can succeed, false otherwise.
     */
    [[nodiscard]] virtual bool seekable() const = 0;

    /**
     * @brief Whether this source has encountered a fatal read error.
     *
     * The flag is sticky: once true, the source's read state is no longer
     * trusted. Callers should stop transmitting and report an error rather
     * than attempting to rewind and continue.
     *
     * @return true if any read has encountered a fatal I/O / decoder error.
     */
    [[nodiscard]] virtual bool error() const = 0;

protected:
    AudioSource() = default;
};
