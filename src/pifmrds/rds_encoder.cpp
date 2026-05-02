/**
 * @file rds_encoder.cpp
 * @brief RDS group encoder implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 25.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "rds_encoder.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace {
    /**
     * @brief Pack two bytes into a 16-bit value, big-endian (MSB first).
     */
    [[nodiscard]] constexpr uint16_t packUint16BigEndian(uint8_t high, uint8_t low) {
        return static_cast<uint16_t>((static_cast<uint16_t>(high) << 8U) | static_cast<uint16_t>(low));
    }

    /**
     * @brief RDS group block indexes.
     */
    enum RdsBlock : std::size_t {
        BLOCK_A = 0,
        BLOCK_B = 1,
        BLOCK_C = 2,
        BLOCK_D = 3,
    };

    /**
     * @brief CRC generator polynomial (EN 50067 3.2.1.4).
     *
     * The polynomial is x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + x^0 = 0x5B9.
     * The 0x1B9 form drops the implicit x^10 leading bit so the value fits
     * in the same 10-bit register as the running CRC.
     */
    constexpr uint16_t CRC_POLY{0x1B9};

    /**
     * @brief MSB of a 16-bit information block.
     */
    constexpr uint16_t BLOCK_MSB{0x8000};

    /**
     * @brief AF (Alternative Frequencies) field signalling "no AF list".
     *
     * EN 50067 3.1.5.4: 0xCD in the high byte and low byte means "method A,
     * 0 frequencies" - the canonical encoding for stations that do not
     * advertise an alternative-frequency list.
     */
    constexpr uint16_t AF_NO_LIST{0xCDCD};

    /**
     * @brief TP (Traffic Programme) flag in block B.
     *
     * Kept enabled to preserve the previous stream semantics; unlike TA,
     * TP is a station capability bit and is common to all group types here.
     */
    constexpr uint16_t TP_BIT{0x0400};

    /**
     * @brief Group type / version code for 0A groups (PS).
     *
     * Block B layout (high 5 bits = type/version): 0A = group type 0,
     * version A (i.e. 0b00000xxxxxxxxxxx). TP is applied separately via
     * TP_BIT; bits below carry PTY/TA/MS/DI flags and PS segment index.
     */
    constexpr uint16_t GROUP_0A_TYPE_VERSION{0x0000};

    /**
     * @brief Group type / version code for 2A groups (RT).
     *
     * Block B layout: 2A = group type 2, version A (0b00100xxxxxxxxxxx).
     * TP is applied separately via TP_BIT; low bits carry PTY, A_B flag,
     * and RT segment index (bits 0..3).
     */
    constexpr uint16_t GROUP_2A_TYPE_VERSION{0x2000};

    /**
     * @brief Group type / version code for 4A groups (CT).
     *
     * Block B layout: 4A = group type 4, version A (0b01000xxxxxxxxxxx).
     * TP is applied separately via TP_BIT; block B's low 2 bits carry the
     * high 2 bits of the Modified Julian Date.
     */
    constexpr uint16_t GROUP_4A_TYPE_VERSION{0x4000};

    /**
     * @brief Bit position of the TA (Traffic Announcement) flag in 0A block 2.
     */
    constexpr uint16_t TA_BIT{0x0010};

    /**
     * @brief RadioText end-of-message marker (EN 50067 3.1.5.3).
     *
     * When the RT string is shorter than the 64-character buffer, a 0x0D
     * carriage-return byte is inserted right after the last character so
     * receivers can detect end-of-text instead of treating trailing spaces
     * as part of the message (which can leave stale glyphs from a previous
     * RT update on the receiver display).
     */
    constexpr char RT_END_MARKER{0x0D};

    /**
     * @brief Number of group-cycle steps before a 2A (RT) group is emitted.
     *
     * The classical 0A/2A mix: four 0A groups in a row, then one 2A. At
     * 1187.5 bit/s and 104 bits per group, one group lasts ~87.6 ms, so
     * the 5-group cycle is ~438 ms - that gives a full PS update every
     * ~440 ms (4 PS segments per cycle) and a full RT update every ~7 s
     * (16 RT segments at one segment per cycle).
     */
    constexpr int GROUPS_BEFORE_RT{4};

    /**
     * @brief Total period of the 0A/2A cycle (4x 0A + 1x 2A).
     */
    constexpr int GROUP_CYCLE_LENGTH{GROUPS_BEFORE_RT + 1};

    /**
     * @brief Number of PS segments transmitted per full PS rotation.
     */
    constexpr int PS_SEGMENTS{RdsEncoder::PS_LENGTH / 2};

    /**
     * @brief Number of RT segments transmitted per full RT rotation.
     */
    constexpr int RT_SEGMENTS{RdsEncoder::RT_LENGTH / 4};

    /**
     * @brief Local-time offset unit in seconds for the RDS CT field.
     *
     * EN 50067 3.1.5.6 encodes the local-time offset in 30-minute steps
     * (signed), which is 30 * 60 = 1800 seconds.
     */
    constexpr int CT_LOCAL_OFFSET_UNIT_SECONDS{1800};

    /**
     * @brief Sign bit position for the local-time offset (bit 5 of block 3).
     */
    constexpr uint16_t CT_LOCAL_OFFSET_SIGN_BIT{0x20};

    /**
     * @brief Mask for the 5-bit local-time offset magnitude (bits 0..4 of block 3).
     *
     * EN 50067 3.1.5.6 reserves five bits for the magnitude, so the largest
     * representable offset is 31 half-hours (15.5 h). We mask defensively to
     * keep the value from leaking into the minute-of-hour field above.
     * Stored as int because every use site combines it with int magnitude
     * arithmetic; the final OR into block D casts to uint16_t once.
     */
    constexpr int CT_LOCAL_OFFSET_MAGNITUDE_MASK{0x1F};

    /**
     * @brief Current UTC minute decomposed for CT group encoding.
     */
    struct UtcMinuteTime {
        std::chrono::system_clock::time_point now;
        std::chrono::sys_time<std::chrono::minutes> minutePoint;
        std::chrono::sys_days day;
        int hourOfDay;
        int minuteOfHour;
    };

    /**
     * @brief Compute the Modified Julian Date from a UTC day.
     *
     * MJD is the number of whole days since 1858-11-17 00:00 UTC.
     *
     * @param utcDay UTC day.
     * @return Modified Julian Date. Current broadcast-era dates fit in the
     *         RDS CT field's 17-bit MJD range.
     */
    [[nodiscard]] int computeMjd(std::chrono::sys_days utcDay) {
        constexpr std::chrono::sys_days mjdEpoch{std::chrono::year{1858} / std::chrono::month{11} /
                                                 std::chrono::day{17}};

        return static_cast<int>((utcDay - mjdEpoch).count());
    }

    /**
     * @brief Read the system clock and decompose the current UTC minute.
     */
    [[nodiscard]] UtcMinuteTime currentUtcMinuteTime() {
        const auto now{std::chrono::system_clock::now()};
        const auto minutePoint{std::chrono::floor<std::chrono::minutes>(now)};
        const auto day{std::chrono::floor<std::chrono::days>(minutePoint)};
        const std::chrono::hh_mm_ss clockTime{minutePoint - day};

        return {
            .now          = now,
            .minutePoint  = minutePoint,
            .day          = day,
            .hourOfDay    = static_cast<int>(clockTime.hours().count()),
            .minuteOfHour = static_cast<int>(clockTime.minutes().count()),
        };
    }

    /**
     * @brief Get the local UTC offset in RDS CT half-hour units.
     *
     * Uses localtime_r (POSIX) instead of std::localtime to avoid the
     * static-storage tm buffer; tm_gmtoff is a glibc/BSD extension (POSIX
     * after 2024) and is the most reliable way to obtain the local offset
     * because std::chrono time-zone support is not consistently available
     * in the libstdc++ versions shipped on Debian/RPi.
     *
     * @param now Current system-clock time point.
     * @return Local UTC offset in 30-minute steps, or 0 if localtime_r fails.
     */
    [[nodiscard]] int localUtcOffsetHalfHours(std::chrono::system_clock::time_point now) {
        const std::time_t localTime{std::chrono::system_clock::to_time_t(now)};
        std::tm local{};
        if (localtime_r(&localTime, &local) == nullptr) {
            return 0;
        }

        return static_cast<int>(local.tm_gmtoff / CT_LOCAL_OFFSET_UNIT_SECONDS);
    }

    /**
     * @brief Append the requested number of most-significant bits to a flat bit buffer.
     *
     * @param value Source value.
     * @param bitCount Number of bits to append, starting with the highest bit.
     * @param bits Output bit buffer.
     * @param bitIndex Current write index, advanced by bitCount.
     */
    void appendMsbBits(uint16_t value, int bitCount, std::array<int, RDS_BITS_PER_GROUP>& bits, int& bitIndex) {
        for (int bit{bitCount - 1}; bit >= 0; --bit) {
            const uint16_t mask{static_cast<uint16_t>(uint16_t{1} << bit)};
            bits[static_cast<std::size_t>(bitIndex++)] = static_cast<int>((value & mask) != 0);
        }
    }
}  // namespace

void RdsEncoder::setPs(std::string_view ps) {
    ps_.fill(' ');
    const auto charsToCopy{std::min(ps.size(), static_cast<std::size_t>(PS_LENGTH))};
    std::copy_n(ps.begin(), charsToCopy, ps_.begin());
}

void RdsEncoder::setRt(std::string_view rt) {
    rt_.fill(' ');
    const auto charsToCopy{std::min(rt.size(), static_cast<std::size_t>(RT_LENGTH))};
    std::copy_n(rt.begin(), charsToCopy, rt_.begin());
    if (charsToCopy < static_cast<std::size_t>(RT_LENGTH)) {
        rt_[charsToCopy] = RT_END_MARKER;
    }
}

uint16_t RdsEncoder::crc(uint16_t block) {
    // Standard shift-register CRC: shift each bit of the input through the
    // 10-bit register and XOR the polynomial whenever the MSB feedback is 1.
    uint16_t reg{0};
    for (int j{0}; j < RDS_BLOCK_BITS; ++j) {
        const int bit{static_cast<int>((block & BLOCK_MSB) != 0)};
        block <<= 1;

        const int msb{(reg >> (RDS_CRC_BITS - 1)) & 1};
        reg <<= 1;
        if ((msb ^ bit) != 0) {
            reg ^= CRC_POLY;
        }
    }
    return static_cast<uint16_t>(reg & 0x3FF);
}

bool RdsEncoder::tryFillCtGroup(std::array<uint16_t, RDS_BLOCKS_PER_GROUP>& blocks) {
    // CT (clock time, group 4A) is emitted exactly once per UTC minute. We
    // reach for the system clock every group call (~11 Hz at 1187.5 bit/s
    // and 104 bits per group, negligible cost) rather than maintaining a
    // parallel timer. UTC fields are derived with std::chrono; local-offset
    // lookup still goes through the platform time-zone database via the
    // localUtcOffsetHalfHours() helper.
    const auto utc{currentUtcMinuteTime()};

    // Skip the very first CT emission: receivers benefit from seeing PS as
    // soon as possible, so we anchor lastCtUtcMinute_ to the current minute
    // and let the next minute boundary trigger the first 4A group.
    if (lastCtUtcMinute_.has_value() == false) {
        lastCtUtcMinute_ = utc.minutePoint;
        return false;
    }
    if (lastCtUtcMinute_ == utc.minutePoint) {
        return false;
    }
    lastCtUtcMinute_ = utc.minutePoint;

    const int mjd{computeMjd(utc.day)};

    blocks[BLOCK_B] = static_cast<uint16_t>(GROUP_4A_TYPE_VERSION | TP_BIT | (mjd >> 15));
    blocks[BLOCK_C] = static_cast<uint16_t>((mjd << 1) | (utc.hourOfDay >> 4));
    blocks[BLOCK_D] = static_cast<uint16_t>(((utc.hourOfDay & 0xF) << 12) | (utc.minuteOfHour << 6));

    // Local-offset half-hours are encoded as magnitude plus a separate sign bit.
    // The magnitude field is only 5 bits wide (max 31 half-hours), so we clamp
    // and mask to keep stray bits out of the minute-of-hour field above.
    const int offset{localUtcOffsetHalfHours(utc.now)};
    int offsetMagnitude{offset};
    if (offsetMagnitude < 0) {
        offsetMagnitude = -offsetMagnitude;
    }
    if (offsetMagnitude > CT_LOCAL_OFFSET_MAGNITUDE_MASK) {
        offsetMagnitude = CT_LOCAL_OFFSET_MAGNITUDE_MASK;
    }
    blocks[BLOCK_D] = static_cast<uint16_t>(blocks[BLOCK_D] |
                                            static_cast<uint16_t>(offsetMagnitude & CT_LOCAL_OFFSET_MAGNITUDE_MASK));
    if (offset < 0) {
        blocks[BLOCK_D] = static_cast<uint16_t>(blocks[BLOCK_D] | CT_LOCAL_OFFSET_SIGN_BIT);
    }
    return true;
}

void RdsEncoder::fillPsGroup(std::array<uint16_t, RDS_BLOCKS_PER_GROUP>& blocks) {
    blocks[BLOCK_B] = static_cast<uint16_t>(GROUP_0A_TYPE_VERSION | TP_BIT | psSegment_);
    if (ta_) {
        blocks[BLOCK_B] = static_cast<uint16_t>(blocks[BLOCK_B] | TA_BIT);
    }
    blocks[BLOCK_C] = AF_NO_LIST;
    // Two consecutive PS chars per segment, MSB byte first.
    const auto hi{static_cast<uint8_t>(ps_[static_cast<std::size_t>(psSegment_ * 2)])};
    const auto lo{static_cast<uint8_t>(ps_[static_cast<std::size_t>(psSegment_ * 2 + 1)])};
    blocks[BLOCK_D] = packUint16BigEndian(hi, lo);

    psSegment_ = (psSegment_ + 1) % PS_SEGMENTS;
}

void RdsEncoder::fillRtGroup(std::array<uint16_t, RDS_BLOCKS_PER_GROUP>& blocks) {
    blocks[BLOCK_B] = static_cast<uint16_t>(GROUP_2A_TYPE_VERSION | TP_BIT | rtSegment_);
    // Four consecutive RT chars per segment.
    const auto c0{static_cast<uint8_t>(rt_[static_cast<std::size_t>(rtSegment_ * 4 + 0)])};
    const auto c1{static_cast<uint8_t>(rt_[static_cast<std::size_t>(rtSegment_ * 4 + 1)])};
    const auto c2{static_cast<uint8_t>(rt_[static_cast<std::size_t>(rtSegment_ * 4 + 2)])};
    const auto c3{static_cast<uint8_t>(rt_[static_cast<std::size_t>(rtSegment_ * 4 + 3)])};
    blocks[BLOCK_C] = packUint16BigEndian(c0, c1);
    blocks[BLOCK_D] = packUint16BigEndian(c2, c3);

    rtSegment_ = (rtSegment_ + 1) % RT_SEGMENTS;
}

void RdsEncoder::serializeBlocks(const std::array<uint16_t, RDS_BLOCKS_PER_GROUP>& blocks,
                                 std::array<int, RDS_BITS_PER_GROUP>& bits) {
    int bitIndex{0};
    for (int i{0}; i < RDS_BLOCKS_PER_GROUP; ++i) {
        const uint16_t block{blocks[static_cast<std::size_t>(i)]};
        const uint16_t check{static_cast<uint16_t>(crc(block) ^ OFFSET_WORDS[static_cast<std::size_t>(i)])};

        appendMsbBits(block, RDS_BLOCK_BITS, bits, bitIndex);
        appendMsbBits(check, RDS_CRC_BITS, bits, bitIndex);
    }
}

void RdsEncoder::nextGroupBits(std::array<int, RDS_BITS_PER_GROUP>& bits) {
    std::array<uint16_t, RDS_BLOCKS_PER_GROUP> blocks{};
    blocks[BLOCK_A] = pi_;

    if (tryFillCtGroup(blocks) == false) {
        if (groupState_ < GROUPS_BEFORE_RT) {
            fillPsGroup(blocks);
        } else {
            fillRtGroup(blocks);
        }
        groupState_ = (groupState_ + 1) % GROUP_CYCLE_LENGTH;
    }

    serializeBlocks(blocks, bits);
}
