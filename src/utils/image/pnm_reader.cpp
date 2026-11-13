/**
 * @file pnm_reader.cpp
 * @brief Binary PGM (P5) / PPM (P6) stream reader implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 09.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "pnm_reader.h"

#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace {
    /**
     * @brief End-of-stream marker returned by std::istream::peek() and get().
     */
    constexpr int END_OF_STREAM{std::istream::traits_type::eof()};

    /**
     * @brief Largest sample value stored in one byte per sample.
     */
    constexpr int ONE_BYTE_MAX_VALUE{255};

    /**
     * @brief Whether character separates PGM / PPM header fields.
     */
    [[nodiscard]] bool isPnmWhitespace(int character) noexcept {
        return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == '\v' ||
               character == '\f';
    }
}  // namespace

PnmReader::PnmReader(std::unique_ptr<std::istream> stream, PnmFormat format, bool seekable,
                     std::string descriptionPrefix)
    : stream_{std::move(stream)},
      formatName_{format == PnmFormat::Pixmap ? "PPM" : "PGM"},
      magicDigit_{format == PnmFormat::Pixmap ? '6' : '5'},
      samplesPerPixel_{format == PnmFormat::Pixmap ? 3U : 1U},
      seekable_{seekable},
      descriptionPrefix_{std::move(descriptionPrefix)} {
    if (stream_ == nullptr) {
        throw std::invalid_argument{"PnmReader: stream must not be null"};
    }
}

bool PnmReader::skipWhitespaceAndComments() {
    while (true) {
        const int next{stream_->peek()};
        if (next == END_OF_STREAM) {
            return stream_->bad() == false;
        }
        if (isPnmWhitespace(next)) {
            stream_->get();
        } else if (next == '#') {
            int character{stream_->get()};
            while (character != END_OF_STREAM && character != '\n' && character != '\r') {
                character = stream_->get();
            }
        } else {
            return true;
        }
    }
}

bool PnmReader::readHeaderField(std::string_view fieldName, int maxValue, int& value) {
    if (skipWhitespaceAndComments() == false) {
        lastError_ = "failed to read the " + formatName_ + " header";
        return false;
    }

    int parsed{0};
    int digits{0};
    bool overflowed{false};
    for (int next{stream_->peek()}; next >= '0' && next <= '9'; next = stream_->peek()) {
        stream_->get();
        ++digits;
        // Stop accumulating once the limit is exceeded so long fields cannot overflow.
        if (parsed <= maxValue) {
            parsed = parsed * 10 + (next - '0');
        } else {
            overflowed = true;
        }
    }

    if (digits == 0) {
        lastError_ = "invalid " + formatName_ + " header: missing " + std::string{fieldName};
        return false;
    }
    if (parsed < 1 || parsed > maxValue) {
        lastError_ = "invalid " + formatName_ + " header: " + std::string{fieldName} + " must be in [1, " +
                     std::to_string(maxValue) + "], got " + (overflowed ? "a larger value" : std::to_string(parsed));
        return false;
    }
    value = parsed;
    return true;
}

PnmReadStatus PnmReader::fail(std::string message) {
    lastError_ = std::move(message);
    return PnmReadStatus::Error;
}

PnmReadStatus PnmReader::readHeader() {
    // Skip the rows of the previous image that the consumer did not read.
    while (rowsRemaining_ > 0) {
        stream_->ignore(static_cast<std::streamsize>(rawRow_.size()));
        if (static_cast<std::size_t>(stream_->gcount()) != rawRow_.size()) {
            return fail("truncated " + formatName_ + " raster");
        }
        --rowsRemaining_;
    }

    if (skipWhitespaceAndComments() == false) {
        return fail("failed to read the " + formatName_ + " header");
    }
    if (stream_->peek() == END_OF_STREAM) {
        return PnmReadStatus::End;
    }

    std::array<char, 2> magic{};
    stream_->read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (stream_->gcount() != static_cast<std::streamsize>(magic.size()) || magic[0] != 'P' || magic[1] != magicDigit_) {
        if (magic[0] == 'P' && magic[1] >= '1' && magic[1] <= '7') {
            return fail(std::string{"unsupported image format P"} + magic[1] + ": only binary " + formatName_ + " (P" +
                        magicDigit_ + ") is supported");
        }
        return fail("invalid " + formatName_ + " header: magic number P" + magicDigit_ + " not found");
    }

    PnmHeader parsedHeader{};
    if (readHeaderField("width", PNM_MAX_DIMENSION, parsedHeader.width) == false ||
        readHeaderField("height", PNM_MAX_DIMENSION, parsedHeader.height) == false ||
        readHeaderField("maximum sample value", PNM_MAX_SAMPLE_VALUE, parsedHeader.maxValue) == false) {
        return PnmReadStatus::Error;
    }

    // A single whitespace character separates the header from the raster; netpbm also
    // allows a comment here, whose terminating line break is that whitespace.
    int separator{stream_->get()};
    if (separator == '#') {
        do {
            separator = stream_->get();
        } while (separator != END_OF_STREAM && separator != '\n' && separator != '\r');
    }
    if (isPnmWhitespace(separator) == false) {
        return fail("invalid " + formatName_ + " header: a single whitespace character must precede the raster");
    }

    header_        = parsedHeader;
    rowsRemaining_ = parsedHeader.height;
    const std::size_t bytesPerSample{parsedHeader.maxValue > ONE_BYTE_MAX_VALUE ? 2U : 1U};
    rawRow_.resize(static_cast<std::size_t>(parsedHeader.width) * samplesPerPixel_ * bytesPerSample);
    return PnmReadStatus::Ok;
}

PnmReadStatus PnmReader::readRow(std::span<std::uint8_t> row) {
    if (rowsRemaining_ <= 0) {
        throw std::logic_error{"PnmReader::readRow: no row is pending, call readHeader() first"};
    }
    const std::size_t rowSamples{static_cast<std::size_t>(header_.width) * samplesPerPixel_};
    if (row.size() != rowSamples) {
        throw std::invalid_argument{"PnmReader::readRow: row size " + std::to_string(row.size()) +
                                    " does not match the " + std::to_string(rowSamples) + " samples of an image row"};
    }

    stream_->read(reinterpret_cast<char*>(rawRow_.data()), static_cast<std::streamsize>(rawRow_.size()));
    if (static_cast<std::size_t>(stream_->gcount()) != rawRow_.size()) {
        return fail("truncated " + formatName_ + " raster");
    }
    --rowsRemaining_;

    const int maxValue{header_.maxValue};
    const bool twoBytes{maxValue > ONE_BYTE_MAX_VALUE};
    for (std::size_t i{0}; i < row.size(); ++i) {
        int value{twoBytes ? (rawRow_[2 * i] << 8) | rawRow_[2 * i + 1] : rawRow_[i]};
        if (value > maxValue) {
            value = maxValue;
        }
        // Rescale to 8 bits with rounding; the common maxValue 255 maps 1:1.
        row[i] = static_cast<std::uint8_t>(
            maxValue == ONE_BYTE_MAX_VALUE ? value : (value * ONE_BYTE_MAX_VALUE + maxValue / 2) / maxValue);
    }
    return PnmReadStatus::Ok;
}

bool PnmReader::rewind() {
    if (seekable_ == false) {
        return false;
    }
    stream_->clear();
    stream_->seekg(0);
    if (stream_->fail()) {
        lastError_ = "failed to seek to the start of the " + formatName_ + " stream";
        return false;
    }
    rowsRemaining_ = 0;
    return true;
}

std::string PnmReader::description() const {
    return descriptionPrefix_ + formatName_ + " " + std::to_string(header_.width) + "x" +
           std::to_string(header_.height) + ", " + (header_.maxValue > ONE_BYTE_MAX_VALUE ? "16" : "8") + "-bit";
}

std::unique_ptr<PnmReader> makeFilePnmReader(PnmFormat format, const std::string& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        std::cerr << "[ERROR] Failed to open image file '" << path << "': it is a directory" << std::endl;
        return nullptr;
    }

    auto stream{std::make_unique<std::ifstream>(path, std::ios::binary)};
    if (stream->is_open() == false) {
        std::cerr << "[ERROR] Failed to open image file '" << path << "': " << std::generic_category().message(errno)
                  << std::endl;
        return nullptr;
    }

    // Only a regular file can be rewound for loop playback; a FIFO or device path cannot.
    const bool seekable{std::filesystem::is_regular_file(path, ec)};
    return std::make_unique<PnmReader>(std::move(stream), format, seekable, "");
}

std::unique_ptr<PnmReader> makeStdinPnmReader(PnmFormat format) {
    // A separate istream over the stdin buffer: std::cin itself is owned by the runtime.
    return std::make_unique<PnmReader>(std::make_unique<std::istream>(std::cin.rdbuf()), format, false, "stdin / ");
}

std::unique_ptr<PnmReader> makePnmReader(PnmFormat format, bool useStdin, const std::string& path) {
    if (useStdin) {
        return makeStdinPnmReader(format);
    }
    return makeFilePnmReader(format, path);
}
