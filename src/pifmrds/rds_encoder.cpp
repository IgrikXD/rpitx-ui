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
#include <cstdlib>
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
     * @brief Compute the Modified Julian Date from a broken-down UTC time.
     *
     * Standard MJD formula from EN 50067 §3.1.5.6 informative annex
     * (originally Fliegel & van Flandern, Communications of the ACM 11(10),
     * 1968):
     *
     *   l   = (mon <= Feb) ? 1 : 0                   // fold Jan/Feb into the
     *                                                // previous year so they
     *                                                // become months 13/14
     *   mjd = 14956 + day
     *       + int((year - l) * 365.25)               // year contribution
     *       + int((mon + 2 + l * 12) * 30.6001)      // month contribution
     *
     * The four constants (14956, 365.25, 30.6001, +2 month shift) are
     * inseparable - they are co-calibrated so that the integer truncations
     * produce correct MJDs for every Gregorian date from 1900-03-01 onward
     * (well past the FM-broadcast era). std::tm uses 0-indexed months and
     * year-since-1900, which is exactly what the formula expects, so no
     * pre-conversion is needed.
     *
     * @param utc Broken-down UTC time from std::gmtime.
     * @return Modified Julian Date as a 17-bit-wide unsigned value (the
     *         upper bound is well above 2^17 only beyond year 4500, which
     *         is comfortably out of scope for an FM-broadcast transmitter).
     */
    [[nodiscard]] int computeMjd(const std::tm& utc) {
        const int l{utc.tm_mon <= 1 ? 1 : 0};
        return 14956 + utc.tm_mday + static_cast<int>(static_cast<double>(utc.tm_year - l) * 365.25) +
               static_cast<int>(static_cast<double>(utc.tm_mon + 2 + l * 12) * 30.6001);
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
        const int bit{(block & BLOCK_MSB) != 0 ? 1 : 0};
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
    // reach for std::time() every group call (~11 Hz at 1187.5 bit/s and
    // 104 bits per group, negligible cost) rather than maintaining a
    // parallel timer because the C library's wall-clock helpers are the
    // most honest source of UTC once daylight-saving boundaries are in
    // scope.
    const std::time_t now{std::time(nullptr)};
    const std::tm utc{*std::gmtime(&now)};

    if (utc.tm_min == lastCtMinute_) {
        return false;
    }
    lastCtMinute_ = utc.tm_min;

    const int mjd{computeMjd(utc)};

    blocks[1] = static_cast<uint16_t>(GROUP_4A_HEADER | (mjd >> 15));
    blocks[2] = static_cast<uint16_t>((mjd << 1) | (utc.tm_hour >> 4));
    blocks[3] = static_cast<uint16_t>(((utc.tm_hour & 0xF) << 12) | (utc.tm_min << 6));

    // Local-offset half-hours, encoded with a sign bit at position 5.
    // tm_gmtoff is a glibc/BSD extension (POSIX after 2024) - portable
    // enough for the Raspbian/Debian targets the wider rpitx-ui project
    // already requires.
    const std::tm local{*std::localtime(&now)};
    const int offset{static_cast<int>(local.tm_gmtoff / CT_LOCAL_OFFSET_UNIT_SECONDS)};
    blocks[3] = static_cast<uint16_t>(blocks[3] | std::abs(offset));
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
            bits[static_cast<std::size_t>(idx++)] = (block & (1 << (RDS_BLOCK_BITS - 1))) != 0 ? 1 : 0;
            block                                 = static_cast<uint16_t>(block << 1);
        }
        // 10 CRC bits, MSB-first.
        for (int j{0}; j < RDS_CRC_BITS; ++j) {
            bits[static_cast<std::size_t>(idx++)] = (check & (1 << (RDS_CRC_BITS - 1))) != 0 ? 1 : 0;
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
