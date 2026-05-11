/**
 * @file test_parse_frequency.cpp
 * @brief Unit tests for rpitx::cli::parseFrequencyHz.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 11.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "cli_common.h"

namespace {
    using rpitx::cli::parseFrequencyHz;
}  // namespace

namespace {
    struct ValidFrequencyTestCase {
        std::string_view name;
        std::string_view input;
        std::uint64_t expectedHz;
    };

    /**
     * @brief Render a ValidFrequencyTestCase as its name plus the raw input text and the
     *        expected Hz result for gtest listings and failure messages.
     */
    void PrintTo(const ValidFrequencyTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {input=\"" << testCase.input << "\", expectedHz=" << testCase.expectedHz << "}";
    }

    /**
     * @brief Inputs the parser must accept: integer decimal notation across several magnitudes,
     *        scientific notation in both lower- and upper-case `e`, decimal-mantissa scientific
     *        whose mathematical result is integer-valued in Hz, a value at the 2^32 boundary
     *        to guard against silent 32-bit truncation, and the largest representable double
     *        strictly below 2^64 to probe the strict-less-than admit-side of the upper bound.
     */
    std::vector<ValidFrequencyTestCase> makeValidFrequencyTestCases() {
        return {
            ValidFrequencyTestCase{"IntegerOne", "1", 1ULL},
            ValidFrequencyTestCase{"IntegerKilohertz", "1000", 1'000ULL},
            ValidFrequencyTestCase{"IntegerHundredMegahertz", "100000000", 100'000'000ULL},
            ValidFrequencyTestCase{"ScientificLowerCaseE", "100e6", 100'000'000ULL},
            ValidFrequencyTestCase{"ScientificUpperCaseE", "100E6", 100'000'000ULL},
            ValidFrequencyTestCase{"DecimalMantissaIntegerHz", "100.0e6", 100'000'000ULL},
            ValidFrequencyTestCase{"FractionalMantissaResolvesToInteger", "100.5e6", 100'500'000ULL},
            ValidFrequencyTestCase{"GigahertzScientific", "1.5e9", 1'500'000'000ULL},
            ValidFrequencyTestCase{"SixDecimalsResolveToInteger", "1.234567e6", 1'234'567ULL},
            ValidFrequencyTestCase{"AtTwoToTheThirtyTwo", "4294967296", 4'294'967'296ULL},
            ValidFrequencyTestCase{"NearTwoToTheSixtyFour", "18446744073709549568", 18'446'744'073'709'549'568ULL},
        };
    }
}  // namespace

class ParseFrequencyHzTest : public ::testing::TestWithParam<ValidFrequencyTestCase> {};

/**
 * @brief Valid frequency text resolves to the corresponding integer Hz value.
 *
 * The input matrix sweeps integer notation across several magnitudes plus scientific
 * notation in lower-case `e`, upper-case `E`, and decimal-mantissa forms whose
 * mathematical result is integer-valued in Hz (e.g. 100.5e6 -> 100_500_000). The
 * AtTwoToTheThirtyTwo case probes the 2^32 boundary and guards against silent 32-bit
 * truncation that smaller cases cannot detect; NearTwoToTheSixtyFour probes the upper
 * boundary at 18_446_744_073_709_549_568 - the largest representable double strictly
 * below 2^64, where the 2048-unit gap to 2^64 equals the double ULP at this magnitude
 * - pinning both the high-end static_cast and the strict `< 2^64` admit-side of the
 * range check. The two assertions split the contract: ASSERT_TRUE pins parse-success
 * and prevents UB in the *parsed dereference below; EXPECT_EQ pins exact-Hz
 * correctness - a regression that returned 0 on success, truncated high bits, or
 * mis-handled the mantissa-to-integer conversion would slip past a has_value()-only
 * check.
 */
TEST_P(ParseFrequencyHzTest, ResolvesToExpectedHz) {
    const auto& testCase{GetParam()};
    const auto parsed{parseFrequencyHz(testCase.input)};

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, testCase.expectedHz);
}

INSTANTIATE_TEST_SUITE_P(ValidInputMatrix, ParseFrequencyHzTest, ::testing::ValuesIn(makeValidFrequencyTestCases()),
                         [](const ::testing::TestParamInfo<ValidFrequencyTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct InvalidFrequencyTestCase {
        std::string_view name;
        std::string_view input;
    };

    /**
     * @brief Render an InvalidFrequencyTestCase as its name plus the rejected input
     *        for gtest listings and failure messages.
     */
    void PrintTo(const InvalidFrequencyTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {input=\"" << testCase.input << "\"}";
    }

    /**
     * @brief Inputs the parser must reject - one named case per distinct failure class,
     *        ordered by which parser gate they trip: truly-empty text; whitespace-only
     *        and wholly-non-numeric input; exponents beyond double's representable range;
     *        trailing non-numeric content (alphabetic, unit suffix, embedded whitespace,
     *        mixed-unit); IEEE-754 inf/nan; non-positive magnitudes in both notations;
     *        values at or above the 2^64 uint64 bound; and sub-integer fractional Hz.
     */
    std::vector<InvalidFrequencyTestCase> makeInvalidFrequencyTestCases() {
        return {
            InvalidFrequencyTestCase{"Empty", ""},
            InvalidFrequencyTestCase{"Whitespace", " "},
            InvalidFrequencyTestCase{"NonNumeric", "abc"},
            InvalidFrequencyTestCase{"ExponentOverflowsDouble", "1e999"},
            InvalidFrequencyTestCase{"TrailingAlpha", "100abc"},
            InvalidFrequencyTestCase{"TrailingUnit", "100Hz"},
            InvalidFrequencyTestCase{"TrailingSpace", "100 "},
            InvalidFrequencyTestCase{"TrailingScientificUnit", "1e6kHz"},
            InvalidFrequencyTestCase{"InfinityLiteral", "inf"},
            InvalidFrequencyTestCase{"NanLiteral", "nan"},
            InvalidFrequencyTestCase{"NegativeInteger", "-1"},
            InvalidFrequencyTestCase{"NegativeScientific", "-1e3"},
            InvalidFrequencyTestCase{"Zero", "0"},
            InvalidFrequencyTestCase{"ZeroDecimal", "0.0"},
            InvalidFrequencyTestCase{"AtTwoToTheSixtyFour", "18446744073709551616"},
            InvalidFrequencyTestCase{"AboveTwoToTheSixtyFour", "1e20"},
            InvalidFrequencyTestCase{"FractionalHzDecimal", "100.5"},
            InvalidFrequencyTestCase{"FractionalHzScientific", "1.55e1"},
        };
    }
}  // namespace

class ParseFrequencyHzRejectionTest : public ::testing::TestWithParam<InvalidFrequencyTestCase> {};

/**
 * @brief Inputs that violate the parser's contract round-trip to std::nullopt.
 *
 * The rejection axis carries one named case per distinct failure class so a regression
 * in any single gate is attributable from the case name alone. Empty falls at the
 * `text.empty()` early-out; Whitespace and NonNumeric fail std::from_chars with
 * errc::invalid_argument (no characters matched the pattern); ExponentOverflowsDouble
 * fails std::from_chars with errc::result_out_of_range (10^999 exceeds double's
 * exponent range); Trailing* cases pass the parse but trip `ptr != last`;
 * InfinityLiteral and NanLiteral fail std::isfinite; Negative* and Zero* magnitudes
 * fail the `value <= 0` gate; AtTwoToTheSixtyFour and AboveTwoToTheSixtyFour fail
 * the `value >= 0x1p64` upper bound (a distinct gate from ExponentOverflowsDouble -
 * that one overflows double's exponent range, these overflow uint64); and
 * FractionalHz* cases fail the std::modf integer-Hz check.
 *
 * Comparing against std::nullopt rather than negating has_value() keeps failure
 * messages readable - gtest's pretty-printer surfaces the wrapped value on mismatch.
 */
TEST_P(ParseFrequencyHzRejectionTest, ReturnsNullopt) {
    const auto& testCase{GetParam()};
    const auto parsed{parseFrequencyHz(testCase.input)};

    EXPECT_EQ(parsed, std::nullopt);
}

INSTANTIATE_TEST_SUITE_P(MalformedInputMatrix, ParseFrequencyHzRejectionTest,
                         ::testing::ValuesIn(makeInvalidFrequencyTestCases()),
                         [](const ::testing::TestParamInfo<InvalidFrequencyTestCase>& info) {
                             return std::string{info.param.name};
                         });
