/**
 * @file fsq_encoder.h
 * @brief Encoding of a message into the tones of an FSQ (Fast Simple QSO) transmission.
 *
 * Follows the FSQ mode and the FSQCall protocol of ZL2AFP and ZL1BPU as the FSQCall
 * program and fldigi send them: a sentence that starts with the callsign of the
 * sender, in a varicode of one or two symbols per character, every symbol sent as a
 * step between two of 33 tones (incremental frequency keying, IFK+).
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace pifsq {
    /**
     * @brief Number of tones of an FSQ transmission.
     */
    inline constexpr int TONE_COUNT{33};

    /**
     * @brief Longest callsign: the FSQCall program and fldigi look for the callsign of the sender in the 19
     *        characters in front of its colon.
     */
    inline constexpr std::size_t MAX_CALLSIGN_LENGTH{19};

    /**
     * @brief Type of a message: the sentence that carries it.
     */
    enum class MessageType : uint8_t {
        Directed,    ///< FSQCall sentence, printed by the station it is directed to.
        Undirected,  ///< FSQ sentence, printed by every station that does not ask for directed ones.
    };

    /**
     * @brief Whether callsign is 1 - MAX_CALLSIGN_LENGTH letters, digits or /.
     *
     * These are the characters both programs accept in the callsign of the sender and none of
     * them ends the callsign of the destination. fldigi takes a sender only when it also looks
     * like a callsign: 3 or more characters with a digit that follows a letter or / and is not
     * the last one.
     */
    [[nodiscard]] bool isCallsign(std::string_view callsign) noexcept;

    /**
     * @brief Whether the FSQ varicode has every character of message: the printable ASCII characters.
     */
    [[nodiscard]] bool isEncodable(std::string_view message) noexcept;

    /**
     * @brief Encode a message as the tones of one FSQ transmission.
     *
     * A sentence opens with two spaces, of which the first one gives the receiver its
     * reference tone, and a line feed, followed by the callsign of the sender and a colon.
     * A directed sentence goes on with the checksum of that callsign as two hexadecimal
     * digits, the callsign of the destination, the space that asks the destination to print
     * the message, the message and the end marker of FSQCall: two spaces, a backspace and
     * two spaces. An undirected sentence goes on with the message, a line feed and a space.
     *
     * @param type        Type of the message.
     * @param callsign    Callsign of the sender, one isCallsign() accepts; sent as it is written.
     * @param destination Callsign of the destination of a directed message, one isCallsign() accepts
     *                    ("allcall" for all stations); not used for an undirected one.
     * @param message     Message, one isEncodable() accepts.
     * @return Numbers of the tones (0 - 32) in the order of transmission, one per symbol.
     *
     * @throws std::invalid_argument when a callsign or the message cannot be encoded.
     */
    [[nodiscard]] std::vector<uint8_t> encodeTransmission(MessageType type, std::string_view callsign,
                                                          std::string_view destination, std::string_view message);
}  // namespace pifsq
