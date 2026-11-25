/**
 * @file test_morse_encoder.cpp
 * @brief Unit tests for the Morse code table lookup and the conversion of Morse patterns into OOK symbols.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 25.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "morse_encoder.h"

namespace {
    struct MorseLookupTestCase {
        std::string_view name;
        char character;
        std::optional<std::string_view> expectedMorse;  ///< std::nullopt for a character outside the table.
    };

    /**
     * @brief Render a MorseLookupTestCase as its name and expected pattern for gtest listings and
     *        failure messages.
     */
    void PrintTo(const MorseLookupTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {morse=\"" << testCase.expectedMorse.value_or("none") << "\"}";
    }

    /**
     * @brief Characters of every kind of the table, both ends of the table among them, and characters outside it.
     */
    std::vector<MorseLookupTestCase> makeMorseLookupTestCases() {
        return {
            MorseLookupTestCase{"Space", ' ', "    "},
            MorseLookupTestCase{"Punctuation", '?', "..--..  "},
            MorseLookupTestCase{"Digit", '5', ".....  "},
            MorseLookupTestCase{"UpperCaseLetter", 'A', ".-  "},
            MorseLookupTestCase{"LowerCaseLetter", 'z', "--..  "},
            MorseLookupTestCase{"AsciiOutsideTheTable", '#', std::nullopt},
            MorseLookupTestCase{"ControlCharacter", '\n', std::nullopt},
            MorseLookupTestCase{"NonAsciiByte", '\xC3', std::nullopt},
        };
    }
}  // namespace

class MorseLookupTest : public ::testing::TestWithParam<MorseLookupTestCase> {};

/**
 * @brief charToMorse returns the ITU pattern of a character of the table, in either case of a letter,
 *        and nothing for a character outside the table.
 */
TEST_P(MorseLookupTest, ReturnsThePatternOfTheTable) {
    const auto& testCase{GetParam()};

    EXPECT_EQ(charToMorse(testCase.character), testCase.expectedMorse);
}

INSTANTIATE_TEST_SUITE_P(CharacterKinds, MorseLookupTest, ::testing::ValuesIn(makeMorseLookupTestCases()),
                         [](const ::testing::TestParamInfo<MorseLookupTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct MorseToCwTestCase {
        std::string_view name;
        std::string_view morse;
        std::string_view expectedCw;
    };

    /**
     * @brief Render a MorseToCwTestCase as its name and expected symbols for gtest listings and
     *        failure messages.
     */
    void PrintTo(const MorseToCwTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {cw=" << testCase.expectedCw << "}";
    }

    /**
     * @brief Patterns of a single element, of a character with both elements, of the word space and
     *        with a symbol the table never holds.
     */
    std::vector<MorseToCwTestCase> makeMorseToCwTestCases() {
        return {
            MorseToCwTestCase{"DitWithCharacterGap", ".  ", "1000"},
            MorseToCwTestCase{"DahWithCharacterGap", "-  ", "111000"},
            MorseToCwTestCase{"DitAndDahWithCharacterGap", ".-  ", "10111000"},
            MorseToCwTestCase{"WordSpace", "    ", "0000"},
            MorseToCwTestCase{"OtherSymbolSkipped", ".x-", "101110"},
        };
    }
}  // namespace

class MorseToCwTest : public ::testing::TestWithParam<MorseToCwTestCase> {};

/**
 * @brief morseToCw keys a dit for one symbol and a dah for three, each followed by one silent symbol,
 *        and turns every gap of the pattern into one more silent symbol.
 */
TEST_P(MorseToCwTest, KeysTheElementsAndTheGaps) {
    const auto& testCase{GetParam()};

    EXPECT_EQ(morseToCw(testCase.morse), testCase.expectedCw);
}

INSTANTIATE_TEST_SUITE_P(MorsePatterns, MorseToCwTest, ::testing::ValuesIn(makeMorseToCwTestCases()),
                         [](const ::testing::TestParamInfo<MorseToCwTestCase>& info) {
                             return std::string{info.param.name};
                         });

/**
 * @brief The word PARIS followed by a word space is 50 symbols long, the reference word of a speed in WPM.
 *
 * A speed of N words per minute sends this word N times a minute, so a symbol lasts 1.2 / N seconds; the
 * Morse transmitters derive their symbol rate from that relation. The word holds dits, dahs and the
 * element, character and word gaps, so a wrong length of any of them changes the symbols.
 */
TEST(MorseEncoderTest, ParisWithWordSpaceIsFiftySymbols) {
    std::string cw;
    for (const char character: std::string_view{"PARIS "}) {
        cw += morseToCw(charToMorse(character).value());
    }

    EXPECT_EQ(cw.size(), 50U);
    EXPECT_EQ(cw,
              "10111011101000"
              "10111000"
              "1011101000"
              "101000"
              "10101000"
              "0000");
}
