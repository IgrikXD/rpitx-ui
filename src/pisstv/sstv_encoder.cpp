/**
 * @file sstv_encoder.cpp
 * @brief SSTV calibration header and scan line encoder implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 13.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "sstv_encoder.h"

#include <bit>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace pisstv {
    namespace {
        /**
         * @brief Duration of each of the two leader tones in milliseconds.
         */
        constexpr double LEADER_MS{300.0};

        /**
         * @brief Duration of the break between the leader tones in milliseconds.
         */
        constexpr double BREAK_MS{10.0};

        /**
         * @brief Duration of a VIS bit in milliseconds.
         */
        constexpr double VIS_BIT_MS{30.0};

        /**
         * @brief Number of bits in a VIS code.
         */
        constexpr int VIS_CODE_BITS{7};

        /**
         * @brief Bytes per pixel of a picture row: red, green, blue.
         */
        constexpr std::size_t BYTES_PER_PIXEL{3};

        /**
         * @brief Frequency step between two adjacent levels of a picture component, in Hz.
         */
        constexpr float HZ_PER_LEVEL{(WHITE_HZ - BLACK_HZ) / 255.0F};

        /**
         * @brief Level of component, 0..255, of a pixel given by its red, green and blue levels.
         */
        [[nodiscard]] constexpr float componentLevel(Component component, float red, float green, float blue) noexcept {
            switch (component) {
                case Component::Red:
                    return red;
                case Component::Green:
                    return green;
                case Component::Blue:
                    return blue;
                case Component::Luma:
                    return 16.0F + (65.738F * red + 129.057F * green + 25.064F * blue) / 256.0F;
                case Component::ChromaRed:
                    return 128.0F + (112.439F * red - 94.154F * green - 18.285F * blue) / 256.0F;
                case Component::ChromaBlue:
                    return 128.0F + (-37.945F * red - 74.494F * green + 112.439F * blue) / 256.0F;
            }
            // Closed enum + -Wswitch guarantees this is unreachable; return 0 only
            // to silence "control reaches end of non-void function" on old toolchains.
            return 0.0F;
        }

        /**
         * @brief Append the scan of one picture component: every pixel averaged over the rows of the scan.
         */
        void appendScan(const SstvMode& mode, const Scan& scan, std::span<const std::uint8_t> rows,
                        std::vector<Tone>& tones) {
            const auto width{static_cast<std::size_t>(mode.width)};
            const auto firstRow{static_cast<std::size_t>(scan.firstRow)};
            const auto rowCount{static_cast<std::size_t>(scan.rowCount)};
            const double pixelMs{scan.timeShare * mode.scanTimeMs / static_cast<double>(width)};

            for (std::size_t column{0}; column < width; ++column) {
                float red{0.0F};
                float green{0.0F};
                float blue{0.0F};
                for (std::size_t row{firstRow}; row < firstRow + rowCount; ++row) {
                    const std::size_t pixel{(row * width + column) * BYTES_PER_PIXEL};
                    red += static_cast<float>(rows[pixel]);
                    green += static_cast<float>(rows[pixel + 1]);
                    blue += static_cast<float>(rows[pixel + 2]);
                }
                const auto averagedRows{static_cast<float>(rowCount)};
                const float level{
                    componentLevel(scan.component, red / averagedRows, green / averagedRows, blue / averagedRows)};
                tones.push_back({BLACK_HZ + level * HZ_PER_LEVEL, pixelMs});
            }
        }
    }  // namespace

    void appendCalibrationHeader(const SstvMode& mode, std::vector<Tone>& tones) {
        tones.push_back({LEADER_HZ, LEADER_MS});
        tones.push_back({SYNC_HZ, BREAK_MS});
        tones.push_back({LEADER_HZ, LEADER_MS});

        // Start bit, the VIS code with its parity bit as the eighth bit, stop bit
        const unsigned parityBit{static_cast<unsigned>(std::popcount(mode.visCode)) % 2U};
        const unsigned visBits{mode.visCode | (parityBit << VIS_CODE_BITS)};
        tones.push_back({SYNC_HZ, VIS_BIT_MS});
        for (int bit{0}; bit <= VIS_CODE_BITS; ++bit) {
            tones.push_back({((visBits >> bit) & 1U) != 0U ? VIS_BIT_ONE_HZ : VIS_BIT_ZERO_HZ, VIS_BIT_MS});
        }
        tones.push_back({SYNC_HZ, VIS_BIT_MS});

        if (mode.startSyncMs > 0.0) {
            tones.push_back({SYNC_HZ, mode.startSyncMs});
        }
    }

    void appendSequence(const SstvMode& mode, std::span<const std::uint8_t> rows, std::vector<Tone>& tones) {
        const std::size_t expectedSize{static_cast<std::size_t>(rowsPerSequence(mode)) *
                                       static_cast<std::size_t>(mode.width) * BYTES_PER_PIXEL};
        if (rows.size() != expectedSize) {
            throw std::invalid_argument{"appendSequence: " + std::to_string(rows.size()) +
                                        " bytes of picture rows given, mode " + std::string{mode.name} + " takes " +
                                        std::to_string(expectedSize)};
        }

        for (const LineSegment& segment: mode.sequence) {
            if (const auto* tone{std::get_if<Tone>(&segment)}; tone != nullptr) {
                tones.push_back(*tone);
            } else {
                appendScan(mode, std::get<Scan>(segment), rows, tones);
            }
        }
    }
}  // namespace pisstv
