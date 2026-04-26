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
     * @brief CRC generator polynomial (EN 50067 §3.2.1.4).
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
     * EN 50067 §3.1.5.4: 0xCD in the high byte and low byte means "method A,
     * 0 frequencies" - the canonical encoding for stations that do not
     * advertise an alternative-frequency list.
     */
    constexpr uint16_t AF_NO_LIST{0xCDCD};

    /**
     * @brief Group type / version code for 0A groups (PS).
     *
     * Block 2 layout (high 5 bits = type/version): 0A = group type 0,
     * version A (i.e. 0b00000xxxxxxxxxxx). Bits below carry TP/PTY/TA/MS/DI
     * flags and PS segment index (bits 0..1).
     */
    constexpr uint16_t GROUP_0A_HEADER{0x0400};

    /**
     * @brief Group type / version code for 2A groups (RT).
     *
     * Block 2 layout: 2A = group type 2, version A (0b00100xxxxxxxxxxx).
     * Low bits carry TP/PTY, A_B flag, and RT segment index (bits 0..3).
     */
    constexpr uint16_t GROUP_2A_HEADER{0x2400};

    /**
     * @brief Group type / version code for 4A groups (CT).
     *
     * Block 2 layout: 4A = group type 4, version A (0b01000xxxxxxxxxxx).
     * Block 1's low 5 bits carry the high 5 bits of the Modified Julian Date.
     */
    constexpr uint16_t GROUP_4A_HEADER{0x4400};

    /**
     * @brief Bit position of the TA (Traffic Announcement) flag in 0A block 2.
     */
    constexpr uint16_t TA_BIT{0x0010};

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
     * EN 50067 §3.1.5.6 encodes the local-time offset in 30-minute steps
     * (signed), which is 30 * 60 = 1800 seconds.
     */
    constexpr int CT_LOCAL_OFFSET_UNIT_SECONDS{30 * 60};

    /**
     * @brief Sign bit position for the local-time offset (bit 5 of block 3).
     */
    constexpr uint16_t CT_LOCAL_OFFSET_SIGN_BIT{0x20};

    /**
     * @brief Compute the Modified Julian Date from a UTC calendar date.
     *
     * MJD is the number of whole days since 1858-11-17 00:00 UTC.
     *
     * @param utcDate UTC calendar date.
     * @return Modified Julian Date. Current broadcast-era dates fit in the
     *         RDS CT field's 17-bit MJD range.
     */
    [[nodiscard]] int computeMjd(const std::chrono::year_month_day& utcDate) {
        constexpr std::chrono::sys_days mjdEpoch{std::chrono::year{1858} / std::chrono::month{11} /
                                                  std::chrono::day{17}};

        return static_cast<int>((std::chrono::sys_days{utcDate} - mjdEpoch).count());
    }
}  // namespace

RdsEncoder::RdsEncoder() : pi_{0x0000}, ta_{false} {
    ps_.fill(' ');
    rt_.fill(' ');
}

void RdsEncoder::setPi(uint16_t pi) {
    pi_ = pi;
}

void RdsEncoder::setPs(std::string_view ps) {
    ps_.fill(' ');
    const auto n{std::min(ps.size(), static_cast<std::size_t>(PS_LENGTH))};
    std::copy_n(ps.begin(), n, ps_.begin());
}

void RdsEncoder::setRt(std::string_view rt) {
    rt_.fill(' ');
    const auto n{std::min(rt.size(), static_cast<std::size_t>(RT_LENGTH))};
    std::copy_n(rt.begin(), n, rt_.begin());
}

void RdsEncoder::setTa(bool ta) {
    ta_ = ta;
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
    // lookup still goes through the platform time-zone database below.
    const auto now{std::chrono::system_clock::now()};
    const auto utcSeconds{std::chrono::floor<std::chrono::seconds>(now)};
    const auto utcDay{std::chrono::floor<std::chrono::days>(utcSeconds)};
    const std::chrono::year_month_day utcDate{utcDay};
    const std::chrono::hh_mm_ss utcTime{utcSeconds - utcDay};
    const int utcHour{static_cast<int>(utcTime.hours().count())};
    const int utcMinute{static_cast<int>(utcTime.minutes().count())};

    if (utcMinute == lastCtMinute_) {
        return false;
    }
    lastCtMinute_ = utcMinute;

    const int mjd{computeMjd(utcDate)};

    blocks[1] = static_cast<uint16_t>(GROUP_4A_HEADER | (mjd >> 15));
    blocks[2] = static_cast<uint16_t>((mjd << 1) | (utcHour >> 4));
    blocks[3] = static_cast<uint16_t>(((utcHour & 0xF) << 12) | (utcMinute << 6));

    // Local-offset half-hours, encoded with a sign bit at position 5.
    // tm_gmtoff is a glibc/BSD extension (POSIX after 2024). We keep this
    // narrow C API bridge because std::chrono time-zone support is not
    // consistently available in the libstdc++ versions used on Debian/RPi.
    const std::time_t localTime{std::chrono::system_clock::to_time_t(now)};
    const std::tm local{*std::localtime(&localTime)};
    const int offset{static_cast<int>(local.tm_gmtoff / CT_LOCAL_OFFSET_UNIT_SECONDS)};
    int offsetMagnitude{offset};
    if (offsetMagnitude < 0) {
        offsetMagnitude = -offsetMagnitude;
    }
    blocks[3] = static_cast<uint16_t>(blocks[3] | offsetMagnitude);
    if (offset < 0) {
        blocks[3] = static_cast<uint16_t>(blocks[3] | CT_LOCAL_OFFSET_SIGN_BIT);
    }
    return true;
}

void RdsEncoder::fillPsGroup(std::array<uint16_t, RDS_BLOCKS_PER_GROUP>& blocks) {
    blocks[1] = static_cast<uint16_t>(GROUP_0A_HEADER | psSegment_);
    if (ta_) {
        blocks[1] = static_cast<uint16_t>(blocks[1] | TA_BIT);
    }
    blocks[2] = AF_NO_LIST;
    // Two consecutive PS chars per segment, MSB byte first.
    const auto hi{static_cast<uint8_t>(ps_[static_cast<std::size_t>(psSegment_ * 2)])};
    const auto lo{static_cast<uint8_t>(ps_[static_cast<std::size_t>(psSegment_ * 2 + 1)])};
    blocks[3] = static_cast<uint16_t>((hi << 8) | lo);

    psSegment_ = (psSegment_ + 1) % PS_SEGMENTS;
}

void RdsEncoder::fillRtGroup(std::array<uint16_t, RDS_BLOCKS_PER_GROUP>& blocks) {
    blocks[1] = static_cast<uint16_t>(GROUP_2A_HEADER | rtSegment_);
    // Four consecutive RT chars per segment.
    const auto c0{static_cast<uint8_t>(rt_[static_cast<std::size_t>(rtSegment_ * 4 + 0)])};
    const auto c1{static_cast<uint8_t>(rt_[static_cast<std::size_t>(rtSegment_ * 4 + 1)])};
    const auto c2{static_cast<uint8_t>(rt_[static_cast<std::size_t>(rtSegment_ * 4 + 2)])};
    const auto c3{static_cast<uint8_t>(rt_[static_cast<std::size_t>(rtSegment_ * 4 + 3)])};
    blocks[2] = static_cast<uint16_t>((c0 << 8) | c1);
    blocks[3] = static_cast<uint16_t>((c2 << 8) | c3);

    rtSegment_ = (rtSegment_ + 1) % RT_SEGMENTS;
}

void RdsEncoder::serializeBlocks(const std::array<uint16_t, RDS_BLOCKS_PER_GROUP>& blocks,
                                 std::array<int, RDS_BITS_PER_GROUP>& bits) {
    int idx{0};
    for (int i{0}; i < RDS_BLOCKS_PER_GROUP; ++i) {
        uint16_t block{blocks[static_cast<std::size_t>(i)]};
        uint16_t check{static_cast<uint16_t>(crc(block) ^ OFFSET_WORDS[static_cast<std::size_t>(i)])};

        // 16 information bits, MSB-first.
        for (int j{0}; j < RDS_BLOCK_BITS; ++j) {
            bits[static_cast<std::size_t>(idx++)] = static_cast<int>((block & (1 << (RDS_BLOCK_BITS - 1))) != 0);
            block                                 = static_cast<uint16_t>(block << 1);
        }
        // 10 CRC bits, MSB-first.
        for (int j{0}; j < RDS_CRC_BITS; ++j) {
            bits[static_cast<std::size_t>(idx++)] = static_cast<int>((check & (1 << (RDS_CRC_BITS - 1))) != 0);
            check                                 = static_cast<uint16_t>(check << 1);
        }
    }
}

void RdsEncoder::nextGroupBits(std::array<int, RDS_BITS_PER_GROUP>& bits) {
    std::array<uint16_t, RDS_BLOCKS_PER_GROUP> blocks{pi_, 0, 0, 0};

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
