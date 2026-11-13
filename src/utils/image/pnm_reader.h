/**
 * @file pnm_reader.h
 * @brief Streaming reader for binary PGM (P5) grayscale and PPM (P6) color images.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 09.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Largest accepted image width and height in pixels.
 */
inline constexpr int PNM_MAX_DIMENSION{65'535};

/**
 * @brief Largest sample value allowed by the PGM / PPM formats (two bytes per sample above 255).
 */
inline constexpr int PNM_MAX_SAMPLE_VALUE{65'535};

/**
 * @brief Binary netpbm image type a PnmReader accepts.
 */
enum class PnmFormat {
    Graymap,  ///< PGM (P5): one gray sample per pixel.
    Pixmap,   ///< PPM (P6): red, green and blue samples per pixel.
};

/**
 * @brief Dimensions and sample depth of one PGM / PPM image.
 */
struct PnmHeader {
    int width;     ///< Pixels per row.
    int height;    ///< Rows per image.
    int maxValue;  ///< Largest sample value: one byte per sample up to 255, two bytes above.
};

/**
 * @brief Outcome of one PnmReader step.
 */
enum class PnmReadStatus {
    Ok,     ///< The requested header or row was read.
    End,    ///< Clean end of the stream between two images.
    Error,  ///< Malformed header, truncated raster, or I/O failure (see PnmReader::lastError()).
};

/**
 * @brief Pull-mode reader for a stream of binary PGM (P5) or PPM (P6) images.
 *
 * A PGM / PPM stream is a sequence of images, each a plain-text header followed
 * by the raw raster (ffmpeg's `-f image2pipe -vcodec pgm` / `-vcodec ppm` emits
 * exactly that). readHeader() parses the next header and readRow() returns the
 * rows of that image one by one as 8-bit samples - one gray value per pixel of
 * a PGM image, red, green and blue of a PPM image; 16-bit samples and non-255
 * maximum values are rescaled to 0..255. Loop semantics live in the consumer:
 * once readHeader() reports End the caller decides whether to rewind() or stop.
 *
 * @code
 * auto reader{makeFilePnmReader(PnmFormat::Graymap, "picture.pgm")};
 * if (reader->readHeader() == PnmReadStatus::Ok) {
 *     std::vector<std::uint8_t> row(static_cast<std::size_t>(reader->header().width) * reader->samplesPerPixel());
 *     while (reader->rowsRemaining() > 0 && reader->readRow(row) == PnmReadStatus::Ok) { ... }
 * }
 * @endcode
 */
class PnmReader {
public:
    /**
     * @brief Construct a reader over an owned binary input stream.
     * @param stream            Open stream positioned at the first header (must be non-null).
     * @param format            Image type the stream has to hold.
     * @param seekable          Whether rewind() may seek the stream back to its start.
     * @param descriptionPrefix Text prepended to description(), e.g. "stdin / ".
     *
     * @throws std::invalid_argument when stream is null.
     */
    PnmReader(std::unique_ptr<std::istream> stream, PnmFormat format, bool seekable, std::string descriptionPrefix);

    PnmReader(const PnmReader&)            = delete;
    PnmReader& operator=(const PnmReader&) = delete;
    PnmReader(PnmReader&&)                 = delete;
    PnmReader& operator=(PnmReader&&)      = delete;

    ~PnmReader() = default;

    /**
     * @brief Parse the header of the next image.
     *
     * Whitespace and `#` comments before the magic number are skipped, so a
     * stream may end with a trailing newline. Any rows of the previous image
     * that were not consumed are skipped first.
     *
     * @return Ok when header() is valid, End at a clean end of the stream,
     *         Error on a malformed header or I/O failure.
     */
    [[nodiscard]] PnmReadStatus readHeader();

    /**
     * @brief Header of the current image; valid after readHeader() returned Ok.
     */
    [[nodiscard]] const PnmHeader& header() const noexcept {
        return header_;
    }

    /**
     * @brief Number of 8-bit samples readRow() returns per pixel: 1 for a PGM image, 3 for a PPM image.
     */
    [[nodiscard]] std::size_t samplesPerPixel() const noexcept {
        return samplesPerPixel_;
    }

    /**
     * @brief Number of rows of the current image not yet returned by readRow().
     */
    [[nodiscard]] int rowsRemaining() const noexcept {
        return rowsRemaining_;
    }

    /**
     * @brief Read the next row of the current image as 8-bit samples.
     * @param row Destination; its size must equal header().width * samplesPerPixel().
     * @return Ok when the row was read, Error on a truncated raster or I/O failure.
     *
     * @throws std::invalid_argument when row.size() differs from header().width * samplesPerPixel().
     * @throws std::logic_error when no row is pending (readHeader() was not
     *         called, or every row of the current image has been read).
     */
    [[nodiscard]] PnmReadStatus readRow(std::span<std::uint8_t> row);

    /**
     * @brief Seek back to the first image of the stream.
     * @return false when the stream is not seekable or the seek fails.
     */
    [[nodiscard]] bool rewind();

    /**
     * @brief Whether rewind() can succeed on this stream.
     */
    [[nodiscard]] bool seekable() const noexcept {
        return seekable_;
    }

    /**
     * @brief Message describing the last Error result.
     */
    [[nodiscard]] std::string_view lastError() const noexcept {
        return lastError_;
    }

    /**
     * @brief One-line description of the current image for startup logging,
     *        e.g. "PGM 320x256, 8-bit"; valid after readHeader() returned Ok.
     */
    [[nodiscard]] std::string description() const;

private:
    /**
     * @brief Skip whitespace and `#` comments; false when the stream fails.
     */
    [[nodiscard]] bool skipWhitespaceAndComments();

    /**
     * @brief Parse an unsigned decimal field of the header into value, rejecting
     *        values above maxValue.
     */
    [[nodiscard]] bool readHeaderField(std::string_view fieldName, int maxValue, int& value);

    /**
     * @brief Record an Error result with its message.
     */
    [[nodiscard]] PnmReadStatus fail(std::string message);

    std::unique_ptr<std::istream> stream_;
    std::string formatName_;  ///< "PGM" or "PPM".
    char magicDigit_;         ///< Second character of the magic number: '5' or '6'.
    std::size_t samplesPerPixel_;
    bool seekable_;
    std::string descriptionPrefix_;
    PnmHeader header_{};
    int rowsRemaining_{0};
    std::vector<std::uint8_t> rawRow_;  ///< Raster bytes of one row (one or two bytes per sample).
    std::string lastError_;
};

/**
 * @brief Open a PGM / PPM file; prints a diagnostic to stderr and returns nullptr on failure.
 */
[[nodiscard]] std::unique_ptr<PnmReader> makeFilePnmReader(PnmFormat format, const std::string& path);

/**
 * @brief Read PGM / PPM images from stdin (never seekable, so loop playback is unsupported).
 */
[[nodiscard]] std::unique_ptr<PnmReader> makeStdinPnmReader(PnmFormat format);

/**
 * @brief Dispatch to makeStdinPnmReader() or makeFilePnmReader() by useStdin.
 */
[[nodiscard]] std::unique_ptr<PnmReader> makePnmReader(PnmFormat format, bool useStdin, const std::string& path);
