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
#include <string>

/**
 * @brief Audio metadata exposed to consumers of an AudioSource.
 *
 * Only the fields that callers actually act on are surfaced here: channel
 * count drives buffer sizing and mono / stereo branching, sample rate drives
 * resampler configuration. Bit depth and format subtype (PCM_16, PCM_24,
 * FLOAT, etc.) are diagnostic-only and exposed via AudioSource::description()
 * so the structure stays minimal and consumers do not have to switch on
 * concrete encodings - the source guarantees float [-1.0, 1.0] output
 * regardless of on-disk representation.
 */
struct AudioFormat {
    int channels;    ///< Channel count (1 = mono, 2 = stereo, ...).
    int sampleRate;  ///< Audio sample rate in Hz.
};

/**
 * @brief Streaming pull-mode audio source producing normalized float samples.
 *
 * Concrete implementations wrap a backing decoder (libsndfile for files /
 * stdin, raw PCM for headerless input, ...). Output is always interleaved
 * float in [-1.0, 1.0] regardless of the on-disk sample format so consumers
 * do not have to know how to convert int16 / int24 / float / etc. into the
 * normalized representation their DSP chain expects.
 *
 * Loop semantics live in the consumer, not in the source: when read()
 * returns 0 the caller distinguishes EOF from error via error() and decides
 * whether to call rewind() or stop. Sources stay policy-free; rewind()
 * fails (returns false) on non-seekable backings, which the caller can
 * pre-check via seekable() to fail fast on `--loop` over stdin.
 */
class AudioSource {
public:
    virtual ~AudioSource() = default;

    AudioSource(const AudioSource&)            = delete;
    AudioSource& operator=(const AudioSource&) = delete;
    AudioSource(AudioSource&&)                 = delete;
    AudioSource& operator=(AudioSource&&)      = delete;

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
     * @return One-line description suitable for console output.
     */
    [[nodiscard]] virtual std::string description() const = 0;

    /**
     * @brief Read up to dst.size() interleaved float samples into dst.
     *
     * For stereo, samples are interleaved L, R, L, R, ... and dst.size()
     * must be a multiple of format().channels. The caller derives frame
     * count via dst.size() / format().channels.
     *
     * Returns the number of float samples actually written. A return value
     * less than dst.size() indicates either clean end-of-stream or a read
     * failure - distinguish via error(). Returns 0 at EOF and on error.
     *
     * @param dst Destination buffer; size must be a multiple of channels.
     * @return Number of float samples written (0 on EOF or error).
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
     * File-backed sources are seekable; stdin-backed sources are not.
     * Used by callers to fail fast at startup when --loop is requested
     * over a non-seekable backing.
     *
     * @return true if rewind() can succeed, false otherwise.
     */
    [[nodiscard]] virtual bool seekable() const = 0;

    /**
     * @brief Whether the most recent read() failed with an I/O error.
     *
     * Read failures and clean EOF both surface as a 0 return from read();
     * the caller distinguishes the two by querying error() afterwards.
     * On a true return, the source's read state is no longer trusted -
     * the caller should stop transmitting and report an error rather than
     * attempting to rewind and continue.
     *
     * @return true if the last read() encountered an I/O error (not EOF).
     */
    [[nodiscard]] virtual bool error() const = 0;

protected:
    AudioSource() = default;
};
