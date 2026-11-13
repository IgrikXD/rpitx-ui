/**
 * @file sstv_mode.h
 * @brief SSTV mode descriptions: VIS code, picture size and the timing sequence of the scan lines.
 *
 * The timings follow "Proposal for SSTV Mode Specifications" (JL Barber N7CXI,
 * Dayton SSTV forum, 2000). The porch in front of the B-Y scan of Robot 72 is
 * sent at 1900 Hz as MMSSTV and QSSTV send it; the paper lists 1500 Hz there.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 13.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace pisstv {
    /**
     * @brief Frequency of a VIS bit set to one, in Hz.
     */
    inline constexpr float VIS_BIT_ONE_HZ{1100.0F};

    /**
     * @brief Frequency of the sync pulses and of the VIS break, start and stop bits, in Hz.
     */
    inline constexpr float SYNC_HZ{1200.0F};

    /**
     * @brief Frequency of a VIS bit set to zero, in Hz.
     */
    inline constexpr float VIS_BIT_ZERO_HZ{1300.0F};

    /**
     * @brief Frequency of a black pixel, in Hz.
     */
    inline constexpr float BLACK_HZ{1500.0F};

    /**
     * @brief Frequency of the leader tone of the calibration header, in Hz.
     */
    inline constexpr float LEADER_HZ{1900.0F};

    /**
     * @brief Frequency of the porch in front of a color difference scan, in Hz: a colorless pixel.
     */
    inline constexpr float CHROMA_PORCH_HZ{1900.0F};

    /**
     * @brief Frequency of a white pixel, in Hz.
     */
    inline constexpr float WHITE_HZ{2300.0F};

    /**
     * @brief Tone of constant frequency.
     */
    struct Tone {
        float frequency;    ///< Frequency in Hz.
        double durationMs;  ///< Duration in milliseconds.
    };

    /**
     * @brief Picture component carried by one scan.
     */
    enum class Component : std::uint8_t {
        Red,
        Green,
        Blue,
        Luma,        ///< Y.
        ChromaRed,   ///< R-Y.
        ChromaBlue,  ///< B-Y.
    };

    /**
     * @brief Scan of one picture component across the picture width, one tone per pixel.
     */
    struct Scan {
        Component component;
        int firstRow{0};        ///< First picture row of the timing sequence the component is taken from.
        int rowCount{1};        ///< Rows averaged into the component.
        double timeShare{1.0};  ///< Duration as a share of the scan time of the mode.
    };

    /**
     * @brief Element of a timing sequence: a sync pulse, porch or separator tone, or a scan.
     */
    using LineSegment = std::variant<Tone, Scan>;

    /**
     * @brief Description of one SSTV mode.
     */
    struct SstvMode {
        std::string_view name;                  ///< Value of --mode.
        std::uint8_t visCode;                   ///< 7-bit VIS code sent in the calibration header.
        int width;                              ///< Pixels per picture row.
        int height;                             ///< Picture rows.
        double scanTimeMs;                      ///< Scan time of a picture component in milliseconds.
        double startSyncMs;                     ///< Sync pulse in front of the first line in milliseconds, 0 for none.
        std::span<const LineSegment> sequence;  ///< Timing sequence repeated down the picture.
    };

    /**
     * @brief Martin: sync pulse at the line break, then green, blue and red.
     */
    inline constexpr std::array<LineSegment, 8> MARTIN_SEQUENCE{{
        Tone{SYNC_HZ, 4.862},
        Tone{BLACK_HZ, 0.572},
        Scan{.component = Component::Green},
        Tone{BLACK_HZ, 0.572},
        Scan{.component = Component::Blue},
        Tone{BLACK_HZ, 0.572},
        Scan{.component = Component::Red},
        Tone{BLACK_HZ, 0.572},
    }};

    /**
     * @brief Scottie: green and blue, then the sync pulse in front of red.
     */
    inline constexpr std::array<LineSegment, 7> SCOTTIE_SEQUENCE{{
        Tone{BLACK_HZ, 1.5},
        Scan{.component = Component::Green},
        Tone{BLACK_HZ, 1.5},
        Scan{.component = Component::Blue},
        Tone{SYNC_HZ, 9.0},
        Tone{BLACK_HZ, 1.5},
        Scan{.component = Component::Red},
    }};

    /**
     * @brief Robot 36: two lines, R-Y of both rows behind the first luma scan and B-Y behind the second.
     */
    inline constexpr std::array<LineSegment, 12> ROBOT_36_SEQUENCE{{
        Tone{SYNC_HZ, 9.0},
        Tone{BLACK_HZ, 3.0},
        Scan{.component = Component::Luma},
        Tone{BLACK_HZ, 4.5},
        Tone{CHROMA_PORCH_HZ, 1.5},
        Scan{.component = Component::ChromaRed, .rowCount = 2, .timeShare = 0.5},
        Tone{SYNC_HZ, 9.0},
        Tone{BLACK_HZ, 3.0},
        Scan{.component = Component::Luma, .firstRow = 1},
        Tone{WHITE_HZ, 4.5},
        Tone{CHROMA_PORCH_HZ, 1.5},
        Scan{.component = Component::ChromaBlue, .rowCount = 2, .timeShare = 0.5},
    }};

    /**
     * @brief Robot 72: luma, R-Y and B-Y of every row.
     */
    inline constexpr std::array<LineSegment, 9> ROBOT_72_SEQUENCE{{
        Tone{SYNC_HZ, 9.0},
        Tone{BLACK_HZ, 3.0},
        Scan{.component = Component::Luma},
        Tone{BLACK_HZ, 4.5},
        Tone{CHROMA_PORCH_HZ, 1.5},
        Scan{.component = Component::ChromaRed, .timeShare = 0.5},
        Tone{WHITE_HZ, 4.5},
        Tone{CHROMA_PORCH_HZ, 1.5},
        Scan{.component = Component::ChromaBlue, .timeShare = 0.5},
    }};

    /**
     * @brief PD: luma of the first row, R-Y and B-Y of both rows, luma of the second row.
     */
    inline constexpr std::array<LineSegment, 6> PD_SEQUENCE{{
        Tone{SYNC_HZ, 20.0},
        Tone{BLACK_HZ, 2.08},
        Scan{.component = Component::Luma},
        Scan{.component = Component::ChromaRed, .rowCount = 2},
        Scan{.component = Component::ChromaBlue, .rowCount = 2},
        Scan{.component = Component::Luma, .firstRow = 1},
    }};

    /**
     * @brief Supported SSTV modes; the first one is the default.
     */
    // clang-format off
    inline constexpr std::array<SstvMode, 9> SSTV_MODES{{
        // name         VIS  width  height  scan ms  start sync ms  sequence
        {"martin-m1",   44,  320,   256,    146.432, 0.0,           MARTIN_SEQUENCE},
        {"martin-m2",   40,  320,   256,    73.216,  0.0,           MARTIN_SEQUENCE},
        {"scottie-s1",  60,  320,   256,    138.240, 9.0,           SCOTTIE_SEQUENCE},
        {"scottie-s2",  56,  320,   256,    88.064,  9.0,           SCOTTIE_SEQUENCE},
        {"robot-36",    8,   320,   240,    88.0,    0.0,           ROBOT_36_SEQUENCE},
        {"robot-72",    12,  320,   240,    138.0,   0.0,           ROBOT_72_SEQUENCE},
        {"pd-90",       99,  320,   256,    170.240, 0.0,           PD_SEQUENCE},
        {"pd-120",      95,  640,   496,    121.600, 0.0,           PD_SEQUENCE},
        {"pd-180",      96,  640,   496,    183.040, 0.0,           PD_SEQUENCE},
    }};
    // clang-format on

    /**
     * @brief Number of picture rows carried by one pass of the timing sequence of mode.
     */
    [[nodiscard]] constexpr int rowsPerSequence(const SstvMode& mode) noexcept {
        int rows{0};
        for (const LineSegment& segment: mode.sequence) {
            if (std::holds_alternative<Scan>(segment)) {
                const Scan& scan{std::get<Scan>(segment)};
                rows = std::max(rows, scan.firstRow + scan.rowCount);
            }
        }
        return rows;
    }

    static_assert(std::ranges::all_of(SSTV_MODES,
                                      [](const SstvMode& mode) { return mode.height % rowsPerSequence(mode) == 0; }),
                  "the timing sequence of every mode must cover its picture in whole passes");

    /**
     * @brief Duration of the picture of mode in milliseconds, without the calibration header.
     */
    [[nodiscard]] constexpr double pictureTimeMs(const SstvMode& mode) noexcept {
        double sequenceTimeMs{0.0};
        for (const LineSegment& segment: mode.sequence) {
            if (std::holds_alternative<Tone>(segment)) {
                sequenceTimeMs += std::get<Tone>(segment).durationMs;
            } else {
                sequenceTimeMs += std::get<Scan>(segment).timeShare * mode.scanTimeMs;
            }
        }
        return mode.startSyncMs + sequenceTimeMs * mode.height / rowsPerSequence(mode);
    }
}  // namespace pisstv
