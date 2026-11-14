/**
 * @file baudot_encoder.cpp
 * @brief Baudot code (ITA2) message encoder implementation.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 14.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "baudot_encoder.h"

namespace pirtty {
    namespace {
        /**
         * @brief Code that switches the receiver to letters case (LTRS), also the idle character of RTTY.
         */
        constexpr std::uint8_t LETTERS_SHIFT{0x1F};

        /**
         * @brief Code that switches the receiver to figures case (FIGS).
         */
        constexpr std::uint8_t FIGURES_SHIFT{0x1B};
    }  // namespace

    EncodedMessage encodeMessage(std::string_view message) {
        EncodedMessage encoded;
        // Case the receiver is in; unknown after a space in figures case.
        std::optional<BaudotCase> receiverCase{BaudotCase::Letters};
        const auto append{[&encoded, &receiverCase](char ch) {
            const auto character{charToBaudot(ch)};
            if (character == std::nullopt) {
                encoded.charactersSkipped = true;
                return;
            }
            if (character->letterCase != BaudotCase::Both && receiverCase != character->letterCase) {
                encoded.codes.push_back(character->letterCase == BaudotCase::Letters ? LETTERS_SHIFT : FIGURES_SHIFT);
                receiverCase = character->letterCase;
            }
            encoded.codes.push_back(character->code);
            if (character->ch == ' ' && receiverCase == BaudotCase::Figures) {
                receiverCase.reset();
            }
        }};

        encoded.codes = {LETTERS_SHIFT, LETTERS_SHIFT};
        append('\r');
        append('\n');
        for (const char ch: message) {
            append(ch);
        }
        append('\r');
        append('\n');
        return encoded;
    }
}  // namespace pirtty
