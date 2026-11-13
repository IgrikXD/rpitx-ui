/**
 * @file test_pnm_reader.cpp
 * @brief Unit tests for the binary PGM (P5) / PPM (P6) stream reader and its factories.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 09.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "captured_streams_mixin.h"
#include "pnm_reader.h"

namespace {
    /**
     * @brief Build an in-memory image: the textual header with the given magic number followed by
     *        the raw raster bytes.
     */
    [[nodiscard]] std::string makeImage(std::string_view magic, int width, int height, int maxValue,
                                        const std::vector<std::uint8_t>& raster) {
        std::string image{std::string{magic} + "\n" + std::to_string(width) + " " + std::to_string(height) + "\n" +
                          std::to_string(maxValue) + "\n"};
        image.append(raster.begin(), raster.end());
        return image;
    }

    /**
     * @brief Build an in-memory PGM image: one gray sample per pixel.
     */
    [[nodiscard]] std::string makePgm(int width, int height, int maxValue, const std::vector<std::uint8_t>& raster) {
        return makeImage("P5", width, height, maxValue, raster);
    }

    /**
     * @brief Build an in-memory PPM image: red, green and blue samples per pixel.
     */
    [[nodiscard]] std::string makePpm(int width, int height, int maxValue, const std::vector<std::uint8_t>& raster) {
        return makeImage("P6", width, height, maxValue, raster);
    }

    /**
     * @brief Wrap in-memory image data into a seekable PnmReader of the given format.
     */
    [[nodiscard]] std::unique_ptr<PnmReader> makeMemoryReader(PnmFormat format, std::string data) {
        return std::make_unique<PnmReader>(std::make_unique<std::istringstream>(std::move(data)), format, true, "");
    }

    /**
     * @brief Read every row of the current image into one vector of 8-bit samples.
     */
    [[nodiscard]] std::vector<std::uint8_t> readAllRows(PnmReader& reader) {
        const auto width{static_cast<std::size_t>(reader.header().width)};
        std::vector<std::uint8_t> pixels;
        std::vector<std::uint8_t> row(width * reader.samplesPerPixel());
        while (reader.rowsRemaining() > 0) {
            if (reader.readRow(row) != PnmReadStatus::Ok) {
                throw std::runtime_error{"readAllRows: " + std::string{reader.lastError()}};
            }
            pixels.insert(pixels.end(), row.begin(), row.end());
        }
        return pixels;
    }

    /**
     * @brief RAII guard that removes the file on destruction.
     */
    struct TempFileGuard {
        std::string path;
        ~TempFileGuard() {
            if (path.empty() == false) {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        }
    };

    /**
     * @brief Write data to a unique temp path so parallel ctest workers do not clash.
     */
    [[nodiscard]] std::string writeTempFile(const std::string& data) {
        std::random_device randomDevice;
        const auto path{
            (std::filesystem::temp_directory_path() / ("rpitx_test_" + std::to_string(randomDevice()) + ".pnm"))
                .string()};
        std::ofstream file{path, std::ios::binary};
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (file.good() == false) {
            throw std::runtime_error{"writeTempFile: write failed"};
        }
        return path;
    }
}  // namespace

/**
 * @brief The constructor rejects a null stream instead of deferring the fault to the first read.
 *
 * A null stream is a programmer error at the construction site, so the contract is an
 * immediate std::invalid_argument rather than a sticky Error that would surface later with
 * a misleading "failed to read the PGM header" message.
 */
TEST(PnmReaderTest, ConstructorRejectsNullStream) {
    EXPECT_THROW(PnmReader(nullptr, PnmFormat::Graymap, false, ""), std::invalid_argument);
}

/**
 * @brief A well-formed 8-bit image exposes its header and yields the raster row by row unchanged.
 *
 * The 3x2 raster holds six distinct values so that a row-order or byte-order slip (rows
 * swapped, columns reversed) changes the readback. maxValue 255 is the 1:1 path, so the
 * expected pixels equal the raster bytes exactly, and rowsRemaining() counting down to 0
 * pins the per-row bookkeeping the consumer loop relies on.
 */
TEST(PnmReaderTest, ReadsHeaderAndRowsOfEightBitImage) {
    const std::vector<std::uint8_t> raster{10, 20, 30, 40, 50, 60};
    const auto pnmReader{makeMemoryReader(PnmFormat::Graymap, makePgm(3, 2, 255, raster))};

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    const PnmHeader& header{pnmReader->header()};
    EXPECT_EQ(header.width, 3);
    EXPECT_EQ(header.height, 2);
    EXPECT_EQ(header.maxValue, 255);
    EXPECT_EQ(pnmReader->rowsRemaining(), 2);
    EXPECT_EQ(pnmReader->samplesPerPixel(), 1U);
    EXPECT_EQ(pnmReader->description(), "PGM 3x2, 8-bit");

    EXPECT_EQ(readAllRows(*pnmReader), raster);
    EXPECT_EQ(pnmReader->rowsRemaining(), 0);
}

/**
 * @brief A PPM image yields three samples per pixel in the red, green, blue order of its raster.
 *
 * The 2x2 raster holds twelve distinct values, so a slip in the row length (one sample per
 * pixel, as for a PGM image), in the row order or in the channel order changes the readback.
 * samplesPerPixel() is the factor the consumer sizes its row buffer with, and a row sized for
 * a gray image must be refused rather than filled with a third of the row.
 */
TEST(PnmReaderTest, ReadsRedGreenBlueSamplesOfPixmap) {
    const std::vector<std::uint8_t> raster{10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
    const auto pnmReader{makeMemoryReader(PnmFormat::Pixmap, makePpm(2, 2, 255, raster))};
    std::vector<std::uint8_t> graySizedRow(2);

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(pnmReader->samplesPerPixel(), 3U);
    EXPECT_EQ(pnmReader->description(), "PPM 2x2, 8-bit");
    EXPECT_THROW(
        { [[maybe_unused]] const PnmReadStatus status{pnmReader->readRow(graySizedRow)}; }, std::invalid_argument);

    EXPECT_EQ(readAllRows(*pnmReader), raster);
    EXPECT_EQ(pnmReader->rowsRemaining(), 0);
}

/**
 * @brief A 16-bit PPM image takes two bytes for each of the three samples of a pixel.
 *
 * The 1x1 raster holds 0x0000, 0x8000 and 0xFFFF. A row buffer sized without the
 * bytes-per-sample or the samples-per-pixel factor leaves raster bytes behind, which the
 * End after the only row rules out, and the big-endian 0x8000 must round to 128.
 */
TEST(PnmReaderTest, ReadsTwoBytesForEachSampleOfSixteenBitPixmap) {
    const std::vector<std::uint8_t> raster{0x00, 0x00, 0x80, 0x00, 0xFF, 0xFF};
    const auto pnmReader{makeMemoryReader(PnmFormat::Pixmap, makePpm(1, 1, 65535, raster))};

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(pnmReader->description(), "PPM 1x1, 16-bit");
    EXPECT_EQ(readAllRows(*pnmReader), (std::vector<std::uint8_t>{0, 128, 255}));
    EXPECT_EQ(pnmReader->readHeader(), PnmReadStatus::End);
}

/**
 * @brief Comments and arbitrary whitespace inside the header are skipped, trailing whitespace
 *        after the raster ends the stream cleanly.
 *
 * The header mixes a comment line, tabs, a carriage return, a comment directly behind a
 * field and a comment right after the maximum value whose line break doubles as the raster
 * delimiter (what netpbm's own reader accepts), and the raster is followed by a newline that
 * strict readers reject. The expected 2x1 readback proves the tokenizer resynchronises on
 * every field, and the final End (not Error) pins the tolerance for a trailing newline.
 */
TEST(PnmReaderTest, SkipsCommentsAndWhitespaceInHeader) {
    std::string pgm{"P5\n# created by a test\n\t2 # width\r\n1\n255# maxval\n"};
    pgm.append({'\x7F', '\x80'});
    pgm += "\n";
    const auto pnmReader{makeMemoryReader(PnmFormat::Graymap, pgm)};

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(pnmReader->header().width, 2);
    EXPECT_EQ(pnmReader->header().height, 1);
    EXPECT_EQ(readAllRows(*pnmReader), (std::vector<std::uint8_t>{0x7F, 0x80}));
    EXPECT_EQ(pnmReader->readHeader(), PnmReadStatus::End);
}

/**
 * @brief A CRLF after the maximum value is not one separator: the CR ends the header and the LF is raster data.
 *
 * netpbm defines exactly one whitespace character between the maximum value and the raster, and
 * libnetpbm and ImageMagick read a CRLF-terminated header the same way, so such a file is
 * malformed rather than a variant to tolerate. The 1x2 picture comes out shifted by one byte -
 * the LF (10) is its first pixel - and the byte left over at the end is reported as a bad header,
 * so the shift surfaces as a diagnostic instead of a silently displaced picture.
 */
TEST(PnmReaderTest, ReadsLineFeedAfterCarriageReturnAsRasterData) {
    std::string pgm{"P5\n1 2\n255\r\n"};
    pgm.append({'\x07', '\x08'});
    const auto pnmReader{makeMemoryReader(PnmFormat::Graymap, pgm)};

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(readAllRows(*pnmReader), (std::vector<std::uint8_t>{10, 7}));
    EXPECT_EQ(pnmReader->readHeader(), PnmReadStatus::Error);
    EXPECT_NE(pnmReader->lastError().find("magic number P5 not found"), std::string_view::npos);
}

namespace {
    struct SampleRescalingTestCase {
        std::string_view name;
        int maxValue;
        std::vector<std::uint8_t> raster;
        std::vector<std::uint8_t> expectedPixels;
        std::string_view expectedDescription;
    };

    /**
     * @brief Render a SampleRescalingTestCase as its name and maximum sample value for gtest
     *        listings and failure messages.
     */
    void PrintTo(const SampleRescalingTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {maxValue=" << testCase.maxValue << "}";
    }

    /**
     * @brief Rasters whose declared maximum differs from 255, each covering black, an interior
     *        point whose rescaling needs rounding (a truncating conversion would be one short),
     *        and a sample above the declared maximum that must clamp to white. The two-byte
     *        cases are big-endian: a little-endian misread of 0x80 0x00 yields 128 / 65535 -> 0.
     */
    std::vector<SampleRescalingTestCase> makeSampleRescalingTestCases() {
        return {
            SampleRescalingTestCase{
                "SixteenBit", 65535, {0x00, 0x00, 0x80, 0x00, 0xFF, 0xFF}, {0, 128, 255}, "PGM 3x1, 16-bit"},
            SampleRescalingTestCase{
                "TwelveBit", 4095, {0x00, 0x00, 0x08, 0x00, 0x10, 0x00}, {0, 128, 255}, "PGM 3x1, 16-bit"},
            SampleRescalingTestCase{"FourBit", 15, {0, 7, 20}, {0, 119, 255}, "PGM 3x1, 8-bit"},
        };
    }
}  // namespace

class PnmReaderSampleRescalingTest : public ::testing::TestWithParam<SampleRescalingTestCase> {};

/**
 * @brief Samples are rescaled to 8 bits with rounding: 32768 / 65535 and 2048 / 4095 both round
 *        to 128 (127 when truncated), 7 / 15 gives 119 exactly, and 4096 / 4095 or 20 / 15 clamp
 *        to 255 instead of overflowing; the description names the sample depth.
 */
TEST_P(PnmReaderSampleRescalingTest, RescalesSamplesToEightBits) {
    const auto& testCase{GetParam()};
    const auto pnmReader{makeMemoryReader(PnmFormat::Graymap, makePgm(3, 1, testCase.maxValue, testCase.raster))};

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(pnmReader->description(), testCase.expectedDescription);
    EXPECT_EQ(readAllRows(*pnmReader), testCase.expectedPixels);
}

INSTANTIATE_TEST_SUITE_P(SampleDepths, PnmReaderSampleRescalingTest,
                         ::testing::ValuesIn(makeSampleRescalingTestCases()),
                         [](const ::testing::TestParamInfo<SampleRescalingTestCase>& info) {
                             return std::string{info.param.name};
                         });

/**
 * @brief Concatenated images are read in sequence, each with its own dimensions.
 *
 * The stream holds a 2x1 image followed by a 1x2 image (the layout ffmpeg's image2pipe PGM
 * output produces); after the first raster the reader must find the second header
 * immediately, without a separator, and report the new width / height before End.
 */
TEST(PnmReaderTest, ReadsConcatenatedImagesInSequence) {
    const std::string pgm{makePgm(2, 1, 255, {1, 2}) + makePgm(1, 2, 255, {3, 4})};
    const auto pnmReader{makeMemoryReader(PnmFormat::Graymap, pgm)};

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(pnmReader->header().width, 2);
    EXPECT_EQ(readAllRows(*pnmReader), (std::vector<std::uint8_t>{1, 2}));

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(pnmReader->header().width, 1);
    EXPECT_EQ(pnmReader->header().height, 2);
    EXPECT_EQ(readAllRows(*pnmReader), (std::vector<std::uint8_t>{3, 4}));

    EXPECT_EQ(pnmReader->readHeader(), PnmReadStatus::End);
}

/**
 * @brief readHeader() skips the unread rows of the previous image before parsing the next header.
 *
 * Only the first of two rows of the first image is consumed; the second header must still
 * be found at the right offset, which fails if the reader resumes parsing in the middle of
 * the first raster (the skipped row holds 'P' '5' bytes to make such a slip look like a valid
 * magic number and surface as a wrong width).
 */
TEST(PnmReaderTest, SkipsUnreadRowsBeforeNextHeader) {
    const std::string pgm{makePgm(2, 2, 255, {1, 2, 'P', '5'}) + makePgm(1, 1, 255, {9})};
    const auto pnmReader{makeMemoryReader(PnmFormat::Graymap, pgm)};
    std::vector<std::uint8_t> row(2);

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    ASSERT_EQ(pnmReader->readRow(row), PnmReadStatus::Ok);

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(pnmReader->header().width, 1);
    EXPECT_EQ(readAllRows(*pnmReader), (std::vector<std::uint8_t>{9}));
}

/**
 * @brief An empty stream is a clean End, not an error.
 *
 * The consumer distinguishes "no picture at all" from a malformed picture through the
 * status, so an empty input (or one holding only whitespace) must report End with no
 * diagnostic attached.
 */
TEST(PnmReaderTest, EmptyStreamReportsEnd) {
    const auto pnmReader{makeMemoryReader(PnmFormat::Graymap, "\n")};

    EXPECT_EQ(pnmReader->readHeader(), PnmReadStatus::End);
}

/**
 * @brief A raster shorter than width x height is reported as an Error with a truncation message.
 *
 * The header announces 2x2 but only three bytes follow; the first row reads fine and the
 * second must fail rather than return a half-filled row, because a silently padded row
 * would be painted as if it were picture content.
 */
TEST(PnmReaderTest, TruncatedRasterReportsError) {
    const auto pnmReader{makeMemoryReader(PnmFormat::Graymap, makePgm(2, 2, 255, {1, 2, 3}))};
    std::vector<std::uint8_t> row(2);

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(pnmReader->readRow(row), PnmReadStatus::Ok);
    EXPECT_EQ(pnmReader->readRow(row), PnmReadStatus::Error);
    EXPECT_NE(pnmReader->lastError().find("truncated"), std::string_view::npos);
}

/**
 * @brief A PPM raster that ends inside a pixel is reported as truncated.
 *
 * The header announces one pixel but only two of its three samples follow - a raster that a
 * reader counting one byte per pixel would accept. The diagnostic names the PPM format.
 */
TEST(PnmReaderTest, TruncatedPixmapRasterReportsError) {
    const auto pnmReader{makeMemoryReader(PnmFormat::Pixmap, makePpm(1, 1, 255, {1, 2}))};
    std::vector<std::uint8_t> row(3);

    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(pnmReader->readRow(row), PnmReadStatus::Error);
    EXPECT_NE(pnmReader->lastError().find("truncated PPM raster"), std::string_view::npos);
}

/**
 * @brief readRow() enforces its calling contract with exceptions.
 *
 * A destination whose size differs from the width is a programmer error (the consumer sized
 * its buffer from the wrong header) and throws std::invalid_argument; calling readRow()
 * before readHeader() has produced a pending row is a sequencing error and throws
 * std::logic_error. Both are distinct from the runtime Error status used for bad input data.
 */
TEST(PnmReaderTest, ReadRowThrowsOnContractViolations) {
    const auto pnmReader{makeMemoryReader(PnmFormat::Graymap, makePgm(2, 1, 255, {1, 2}))};
    std::vector<std::uint8_t> wrongSizeRow(3);
    std::vector<std::uint8_t> row(2);

    EXPECT_THROW({ [[maybe_unused]] const PnmReadStatus status{pnmReader->readRow(row)}; }, std::logic_error);
    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_THROW(
        { [[maybe_unused]] const PnmReadStatus status{pnmReader->readRow(wrongSizeRow)}; }, std::invalid_argument);
}

/**
 * @brief rewind() returns to the first image on a seekable stream and is refused otherwise.
 *
 * After consuming the only image to End, a seekable reader rewinds and yields the same
 * raster again, which is the loop-playback path; a reader constructed as non-seekable
 * refuses the rewind so the caller can fail fast instead of reading past the end forever.
 */
TEST(PnmReaderTest, RewindRestartsSeekableStreamOnly) {
    const std::string pgm{makePgm(2, 1, 255, {5, 6})};
    const auto seekableReader{makeMemoryReader(PnmFormat::Graymap, pgm)};
    const auto streamReader{
        std::make_unique<PnmReader>(std::make_unique<std::istringstream>(pgm), PnmFormat::Graymap, false, "stdin / ")};

    ASSERT_EQ(seekableReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(readAllRows(*seekableReader), (std::vector<std::uint8_t>{5, 6}));
    ASSERT_EQ(seekableReader->readHeader(), PnmReadStatus::End);
    EXPECT_TRUE(seekableReader->rewind());
    ASSERT_EQ(seekableReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(readAllRows(*seekableReader), (std::vector<std::uint8_t>{5, 6}));

    EXPECT_FALSE(streamReader->seekable());
    EXPECT_FALSE(streamReader->rewind());
    ASSERT_EQ(streamReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(streamReader->description(), "stdin / PGM 2x1, 8-bit");
}

namespace {
    struct MalformedHeaderTestCase {
        std::string_view name;
        PnmFormat format;
        std::string_view header;
        std::string_view expectedErrorFragment;
    };

    /**
     * @brief Render a MalformedHeaderTestCase as its name plus the expected diagnostic fragment
     *        for gtest listings and failure messages.
     */
    void PrintTo(const MalformedHeaderTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {expectedErrorFragment=\"" << testCase.expectedErrorFragment << "\"}";
    }

    /**
     * @brief Headers that must be rejected: every other netpbm type, a non-PNM magic, a missing
     *        or zero field, dimensions and sample maximum above the format limits, and a header
     *        whose raster is not introduced by a single whitespace character, for a PGM reader;
     *        a PGM image, a non-PNM magic and a missing field for a PPM reader. Each case names
     *        the fragment the diagnostic must carry so a wrong error branch is caught, not only
     *        the Error status.
     */
    std::vector<MalformedHeaderTestCase> makeMalformedHeaderTestCases() {
        return {
            MalformedHeaderTestCase{"AsciiPgm", PnmFormat::Graymap, "P2\n1 1\n255\n0", "only binary PGM (P5)"},
            MalformedHeaderTestCase{
                "BinaryPpm", PnmFormat::Graymap, "P6\n1 1\n255\nabc", "unsupported image format P6"},
            MalformedHeaderTestCase{"NotPnm", PnmFormat::Graymap, "GIF89a", "magic number P5 not found"},
            MalformedHeaderTestCase{"MissingHeight", PnmFormat::Graymap, "P5\n1\n", "missing height"},
            MalformedHeaderTestCase{
                "ZeroWidth", PnmFormat::Graymap, "P5\n0 1\n255\n", "width must be in [1, 65535], got 0"},
            MalformedHeaderTestCase{
                "WidthTooLarge", PnmFormat::Graymap, "P5\n65536 1\n255\n", "width must be in [1, 65535], got 65536"},
            MalformedHeaderTestCase{"HeightOverflowsInt",
                                    PnmFormat::Graymap,
                                    "P5\n1 99999999999999999999\n255\n",
                                    "height must be in [1, 65535], got a larger value"},
            MalformedHeaderTestCase{
                "MaxValueTooLarge", PnmFormat::Graymap, "P5\n1 1\n65536\n", "maximum sample value must be in"},
            MalformedHeaderTestCase{
                "NoRasterSeparator", PnmFormat::Graymap, "P5\n1 1\n255", "single whitespace character"},
            MalformedHeaderTestCase{"BinaryPgmForPixmap", PnmFormat::Pixmap, "P5\n1 1\n255\n0", "only binary PPM (P6)"},
            MalformedHeaderTestCase{"NotPnmForPixmap", PnmFormat::Pixmap, "GIF89a", "magic number P6 not found"},
            MalformedHeaderTestCase{
                "MissingHeightOfPixmap", PnmFormat::Pixmap, "P6\n1\n", "invalid PPM header: missing height"},
        };
    }
}  // namespace

class PnmReaderMalformedHeaderTest : public ::testing::TestWithParam<MalformedHeaderTestCase> {};

/**
 * @brief Each malformed header is rejected with the Error status and the branch-specific diagnostic.
 */
TEST_P(PnmReaderMalformedHeaderTest, RejectsHeaderWithDiagnostic) {
    const auto& testCase{GetParam()};
    const auto pnmReader{makeMemoryReader(testCase.format, std::string{testCase.header})};

    EXPECT_EQ(pnmReader->readHeader(), PnmReadStatus::Error);
    EXPECT_NE(pnmReader->lastError().find(testCase.expectedErrorFragment), std::string_view::npos)
        << "diagnostic: " << pnmReader->lastError();
}

INSTANTIATE_TEST_SUITE_P(HeaderDefects, PnmReaderMalformedHeaderTest,
                         ::testing::ValuesIn(makeMalformedHeaderTestCases()),
                         [](const ::testing::TestParamInfo<MalformedHeaderTestCase>& info) {
                             return std::string{info.param.name};
                         });

class MakePnmReaderTest : public CapturedStreamsMixin<::testing::Test> {};

/**
 * @brief A regular file opens as a seekable reader that yields the picture written to it.
 *
 * The file is produced by the test itself, so the round trip covers the ifstream wiring of the
 * factory, the regular-file seekability detection, and the empty description prefix for files.
 */
TEST_F(MakePnmReaderTest, OpensRegularFileAsSeekableReader) {
    TempFileGuard guard{writeTempFile(makePgm(2, 1, 255, {7, 8}))};

    const auto pnmReader{makeFilePnmReader(PnmFormat::Graymap, guard.path)};

    ASSERT_NE(pnmReader, nullptr);
    EXPECT_TRUE(pnmReader->seekable());
    ASSERT_EQ(pnmReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(pnmReader->description(), "PGM 2x1, 8-bit");
    EXPECT_EQ(readAllRows(*pnmReader), (std::vector<std::uint8_t>{7, 8}));
    EXPECT_TRUE(capturedStderr().empty());
}

/**
 * @brief A nonexistent path and a directory both yield nullptr with a stderr diagnostic.
 *
 * The CLI relies on the nullptr return to exit with status 1 and on the stderr line to tell
 * the user which path failed; a directory opens successfully as an ifstream on Linux, so it
 * needs its own rejection to avoid a misleading "no PGM image found" later on.
 */
TEST_F(MakePnmReaderTest, ReturnsNullptrAndPrintsDiagnosticForUnusablePaths) {
    const auto missingReader{makeFilePnmReader(PnmFormat::Graymap, "/this/path/does/not/exist/at/all.pgm")};
    const auto directoryReader{makeFilePnmReader(PnmFormat::Graymap, std::filesystem::temp_directory_path().string())};

    EXPECT_EQ(missingReader, nullptr);
    EXPECT_EQ(directoryReader, nullptr);
    EXPECT_NE(capturedStderr().find("does/not/exist"), std::string::npos);
    EXPECT_NE(capturedStderr().find("is a directory"), std::string::npos);
}

/**
 * @brief The dispatcher routes --stdin to a non-seekable stdin reader and a path to the file factory.
 *
 * The stdin reader is created without touching the stream, so its seekable() flag is the
 * observable contract that lets the CLI refuse --loop; the path branch is verified through
 * the same temp file round trip the file factory test uses.
 */
TEST_F(MakePnmReaderTest, DispatchesBetweenStdinAndFile) {
    TempFileGuard guard{writeTempFile(makePgm(1, 1, 255, {3}))};

    const auto stdinReader{makePnmReader(PnmFormat::Graymap, true, "ignored.pgm")};
    const auto fileReader{makePnmReader(PnmFormat::Graymap, false, guard.path)};

    ASSERT_NE(stdinReader, nullptr);
    EXPECT_FALSE(stdinReader->seekable());
    ASSERT_NE(fileReader, nullptr);
    EXPECT_TRUE(fileReader->seekable());
    ASSERT_EQ(fileReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(readAllRows(*fileReader), (std::vector<std::uint8_t>{3}));
}

/**
 * @brief The factories hand the requested format to the reader.
 *
 * One PPM file opens as a pixmap reader that yields its three samples per pixel and is
 * refused by a graymap reader with the format diagnostic, and the stdin reader reports three
 * samples per pixel before any byte is read - a factory that dropped its format argument
 * fails one of the three.
 */
TEST_F(MakePnmReaderTest, CreatesReaderOfRequestedFormat) {
    TempFileGuard guard{writeTempFile(makePpm(1, 1, 255, {7, 8, 9}))};

    const auto pixmapReader{makePnmReader(PnmFormat::Pixmap, false, guard.path)};
    const auto graymapReader{makeFilePnmReader(PnmFormat::Graymap, guard.path)};
    const auto stdinReader{makePnmReader(PnmFormat::Pixmap, true, "ignored.ppm")};

    ASSERT_NE(pixmapReader, nullptr);
    ASSERT_EQ(pixmapReader->readHeader(), PnmReadStatus::Ok);
    EXPECT_EQ(readAllRows(*pixmapReader), (std::vector<std::uint8_t>{7, 8, 9}));
    ASSERT_NE(graymapReader, nullptr);
    EXPECT_EQ(graymapReader->readHeader(), PnmReadStatus::Error);
    EXPECT_NE(graymapReader->lastError().find("unsupported image format P6"), std::string_view::npos);
    ASSERT_NE(stdinReader, nullptr);
    EXPECT_EQ(stdinReader->samplesPerPixel(), 3U);
}
