/**
 * @file rds_encoder.h
 * @brief RDS group encoder (PI / PS / RT / CT) per EN 50067.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 25.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

/**
 * @brief Number of 16-bit information blocks in an RDS group.
 *
 * EN 50067: every group is 4 blocks of 16 information bits, each followed
 * by a 10-bit checkword - 104 bits per group total.
 */
inline constexpr int RDS_BLOCKS_PER_GROUP{4};

/**
 * @brief Information bits per RDS block.
 */
inline constexpr int RDS_BLOCK_BITS{16};

/**
 * @brief CRC checkword length in bits (EN 50067 §3.2.1).
 */
inline constexpr int RDS_CRC_BITS{10};

/**
 * @brief Total bits per RDS group (information + CRC).
 */
inline constexpr int RDS_BITS_PER_GROUP{RDS_BLOCKS_PER_GROUP * (RDS_BLOCK_BITS + RDS_CRC_BITS)};

/**
 * @brief Streaming RDS group encoder.
 *
 * Produces one EN 50067 RDS group at a time, cycling through the standard
 * 0A (PS) / 2A (RT) sequence with the 4A (CT) group inserted whenever the
 * UTC minute rolls over. The cycle pattern is fixed at 4x 0A followed by
 * 1x 2A, which is the canonical mix used by every reference RDS encoder
 * (e.g. PiFmRds, the EBU SDP_T 22 example). At 1187.5 bit/s the 5-group
 * cycle lasts ~438 ms, so a full PS rotates in well under a second
 * (4 of 5 groups are 0A) and a full RT rotates in ~7 s (16 segments at
 * one segment per cycle).
 *
 * The encoder does no modulation - it emits raw bit sequences that the
 * RdsModulator differentially encodes, biphase-shapes, and modulates
 * onto the 57 kHz subcarrier. Splitting concerns this way mirrors the
 * EN 50067 reference architecture and lets the modulator stay free of
 * any group-cycle state.
 *
 * @code
 * RdsEncoder enc{};
 * enc.setPi(0xFFFF);
 * enc.setPs("rpitx-ui");
 * enc.setRt("Hello, world!");
 * std::array<int, RDS_BITS_PER_GROUP> bits{};
 * enc.nextGroupBits(bits);
 * @endcode
 */
class RdsEncoder {
public:
    /**
     * @brief Maximum PS (Programme Service name) length, in characters.
     *
     * EN 50067 §3.1.5.2: PS is exactly 8 7-bit characters, transmitted as
     * 4 segments of 2 characters each across consecutive 0A groups.
     */
    static constexpr int PS_LENGTH{8};

    /**
     * @brief Maximum RT (RadioText) length, in characters.
     *
     * EN 50067 §3.1.5.3: RT is up to 64 characters in mode 2A, transmitted
     * as 16 segments of 4 characters each. Strings shorter than 64 are
     * space-padded to fill the field.
     */
    static constexpr int RT_LENGTH{64};

    /**
     * @brief Construct an encoder with placeholder values.
     *
     * Defaults: PI = 0x0000, PS = 8 spaces, RT = 64 spaces, TA = off.
     * These produce a valid (if uninformative) RDS stream; callers are
     * expected to override them with setPi() / setPs() / setRt() before
     * the first nextGroupBits() call.
     */
    RdsEncoder();

    RdsEncoder(const RdsEncoder&)            = delete;
    RdsEncoder& operator=(const RdsEncoder&) = delete;
    RdsEncoder(RdsEncoder&&)                 = delete;
    RdsEncoder& operator=(RdsEncoder&&)      = delete;

    /**
     * @brief Set the Programme Identification (PI) code.
     *
     * EN 50067 §3.2.1.1: PI is a 16-bit station identifier transmitted in
     * block 1 of every group.
     *
     * @param pi 16-bit PI code.
     */
    void setPi(uint16_t pi);

    /**
     * @brief Set the Programme Service (PS) name.
     *
     * Strings shorter than PS_LENGTH are space-padded; longer strings are
     * truncated. The PS field is transmitted two characters at a time
     * across four consecutive 0A groups (4 segments x 2 chars = 8 chars).
     *
     * @param ps Programme Service name; up to PS_LENGTH characters used.
     */
    void setPs(std::string_view ps);

    /**
     * @brief Set the RadioText (RT) string.
     *
     * Strings shorter than RT_LENGTH are space-padded; longer strings are
     * truncated. The RT field is transmitted four characters at a time
     * across 16 consecutive 2A groups.
     *
     * @param rt RadioText string; up to RT_LENGTH characters used.
     */
    void setRt(std::string_view rt);

    /**
     * @brief Set the Traffic Announcement (TA) flag.
     * @param ta true to assert TA in 0A groups, false to clear it.
     */
    void setTa(bool ta);

    /**
     * @brief Generate the next RDS group as a flat bit sequence.
     *
     * Group selection rule, evaluated in order on each call:
     *   1. If a CT (4A) group is due (UTC minute changed since the last
     *      emitted CT), produce 4A.
     *   2. Otherwise advance the 5-step state machine: four 0A (PS)
     *      groups followed by one 2A (RT) group.
     *
     * @param bits Output buffer of exactly RDS_BITS_PER_GROUP entries.
     *             Each entry is 0 or 1.
     */
    void nextGroupBits(std::array<int, RDS_BITS_PER_GROUP>& bits);

private:
    /**
     * @brief Compute the EN 50067 §3.2.1 checkword for a 16-bit data block.
     *
     * The generator polynomial is x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + 1,
     * encoded as 0x1B9 with implicit MSB.
     *
     * @param block 16-bit information block.
     * @return 10-bit CRC checkword (placed in the low 10 bits of the result).
     */
    [[nodiscard]] static uint16_t crc(uint16_t block);

    /**
     * @brief Try to populate the four blocks with a CT (clock-time, 4A) group.
     *
     * Side-effect: updates lastCtMinute_ when a CT group is generated, so
     * the same minute is not transmitted twice. CT is the only RDS group
     * with a wall-clock dependency, so the time read is performed inline
     * here rather than in the caller.
     *
     * @param blocks Output - blocks[1..3] populated when a CT group is due.
     * @return true if a CT group was generated, false otherwise.
     */
    [[nodiscard]] bool tryFillCtGroup(std::array<uint16_t, RDS_BLOCKS_PER_GROUP>& blocks);

    /**
     * @brief Populate the four blocks with a PS-carrying 0A group.
     *
     * Block 2 carries the TA bit and PS segment index; block 3 is the AF
     * code (we transmit 0xCDCD = "no AF list", per EN 50067 §3.1.5.4);
     * block 4 is the two PS characters for this segment.
     *
     * @param blocks Output - blocks[1..3] populated.
     */
    void fillPsGroup(std::array<uint16_t, RDS_BLOCKS_PER_GROUP>& blocks);

    /**
     * @brief Populate the four blocks with an RT-carrying 2A group.
     *
     * Block 2 carries the RT segment index and B0/A_B flags; blocks 3 and
     * 4 are the four RT characters for this segment.
     *
     * @param blocks Output - blocks[1..3] populated.
     */
    void fillRtGroup(std::array<uint16_t, RDS_BLOCKS_PER_GROUP>& blocks);

    /**
     * @brief Serialize the four blocks into a 104-bit MSB-first stream.
     *
     * Each block contributes 16 information bits followed by its 10-bit
     * checkword XORed with the position-specific offset word.
     *
     * @param blocks Information blocks (blocks[0] is always the PI code).
     * @param bits Output - exactly RDS_BITS_PER_GROUP bits written.
     */
    static void serializeBlocks(const std::array<uint16_t, RDS_BLOCKS_PER_GROUP>& blocks,
                                std::array<int, RDS_BITS_PER_GROUP>& bits);

    /**
     * @brief CRC offset words for each block position.
     *
     * EN 50067 §3.2.2 Table 5: every block position uses a different offset
     * word XORed into the checkword to enable block-position synchronisation
     * at the receiver. Offset C' (variant for type-B groups) is omitted -
     * we never emit type-B groups (0B / 2B / 4B etc.).
     */
    static constexpr std::array<uint16_t, RDS_BLOCKS_PER_GROUP> OFFSET_WORDS{0x0FC, 0x198, 0x168, 0x1B4};

    uint16_t pi_;                     ///< PI code (block 1).
    bool ta_;                         ///< Traffic-announcement flag.
    std::array<char, PS_LENGTH> ps_;  ///< 8-char PS, space-padded.
    std::array<char, RT_LENGTH> rt_;  ///< 64-char RT, space-padded.

    /**
     * @brief 5-step group cycle counter: 0..3 emit 0A, 4 emits 2A.
     */
    int groupState_{0};

    /**
     * @brief Current PS segment, in [0, PS_LENGTH/2). Advances after each 0A.
     */
    int psSegment_{0};

    /**
     * @brief Current RT segment, in [0, RT_LENGTH/4). Advances after each 2A.
     */
    int rtSegment_{0};

    /**
     * @brief UTC minute of the last CT group emitted, or -1 if never.
     *
     * Used to gate CT emission to once per minute.
     */
    int lastCtMinute_{-1};
};
