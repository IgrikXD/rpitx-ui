/**
 * @file test_validators.cpp
 * @brief Unit tests for the reusable CLI11 validators.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 12.05.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include <gtest/gtest.h>

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "cli_validators.h"

namespace {
    using rpitx::cli::validators::FrequencyHz;
    using rpitx::cli::validators::PositiveFiniteFloat;

    /**
     * @brief Invoke a CLI11 Validator on a candidate string.
     *
     * Validators expose their callback through operator(); the returned string is empty
     * on accept and a diagnostic on reject. This matches the
     * CLI::App.add_option(...).check(validator) callsite contract.
     */
    std::string runValidator(const CLI::Validator& validator, std::string text) {
        return validator(text);
    }
}  // namespace

namespace {
    struct ValidPositiveFiniteFloatTestCase {
        std::string_view name;
        std::string_view input;
    };

    /**
     * @brief Render a ValidPositiveFiniteFloatTestCase as its name plus the accepted
     *        input for gtest listings and failure messages.
     */
    void PrintTo(const ValidPositiveFiniteFloatTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {input=\"" << testCase.input << "\"}";
    }

    /**
     * @brief Inputs the validator must accept: integer notation, decimal-fraction
     *        notation spanning both below-one and above-one magnitudes, and scientific
     *        notation across lower-case 'e', upper-case 'E', and negative-exponent
     *        forms - each variety the validator's std::from_chars parse step must
     *        handle on the admit side.
     */
    std::vector<ValidPositiveFiniteFloatTestCase> makeValidPositiveFiniteFloatTestCases() {
        return {
            ValidPositiveFiniteFloatTestCase{"IntegerOne", "1"},
            ValidPositiveFiniteFloatTestCase{"IntegerHundred", "100"},
            ValidPositiveFiniteFloatTestCase{"DecimalLessThanOne", "0.5"},
            ValidPositiveFiniteFloatTestCase{"DecimalGreaterThanOne", "3.14"},
            ValidPositiveFiniteFloatTestCase{"ScientificLowerCaseE", "1e2"},
            ValidPositiveFiniteFloatTestCase{"ScientificUpperCaseE", "1E2"},
            ValidPositiveFiniteFloatTestCase{"ScientificNegativeExponent", "1.5e-3"},
        };
    }
}  // namespace

class PositiveFiniteFloatTest : public ::testing::TestWithParam<ValidPositiveFiniteFloatTestCase> {};

/**
 * @brief Valid positive-finite-float text resolves to an empty diagnostic.
 *
 * The negative-exponent case pins sub-1 admit-side coverage that integer-only sweeps cannot
 * reach - a regression that tightened the lower bound (e.g. value >= 1.0F) would slip past
 * "1" and "100" but fail on "0.5" and "1.5e-3". The upper-case 'E' case guards against a
 * case-sensitivity regression in the float overload of std::from_chars.
 *
 * Comparing against a literal empty string rather than negating !empty() keeps failure
 * messages readable - gtest's pretty-printer surfaces the unexpected diagnostic on mismatch.
 */
TEST_P(PositiveFiniteFloatTest, ReturnsEmptyDiagnostic) {
    const auto& testCase{GetParam()};
    EXPECT_EQ(runValidator(PositiveFiniteFloat, std::string{testCase.input}), "");
}

INSTANTIATE_TEST_SUITE_P(ValidInputMatrix, PositiveFiniteFloatTest,
                         ::testing::ValuesIn(makeValidPositiveFiniteFloatTestCases()),
                         [](const ::testing::TestParamInfo<ValidPositiveFiniteFloatTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct InvalidPositiveFiniteFloatTestCase {
        std::string_view name;
        std::string_view input;
    };

    /**
     * @brief Render an InvalidPositiveFiniteFloatTestCase as its name plus the rejected
     *        input for gtest listings and failure messages.
     */
    void PrintTo(const InvalidPositiveFiniteFloatTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {input=\"" << testCase.input << "\"}";
    }

    /**
     * @brief Inputs the validator must reject - one named case per distinct failure
     *        class, grouped by which of the validator's two diagnostics they trigger:
     *        cases producing "must be a numeric value" (empty, whitespace, non-numeric,
     *        trailing garbage, incomplete scientific) come first, followed by cases
     *        producing "must be a positive finite float" (overflow, IEEE-754 inf/nan,
     *        and non-positive magnitudes in decimal and scientific notation).
     */
    std::vector<InvalidPositiveFiniteFloatTestCase> makeInvalidPositiveFiniteFloatTestCases() {
        return {
            InvalidPositiveFiniteFloatTestCase{"Empty", ""},
            InvalidPositiveFiniteFloatTestCase{"Whitespace", " "},
            InvalidPositiveFiniteFloatTestCase{"NonNumeric", "abc"},
            InvalidPositiveFiniteFloatTestCase{"TrailingAlpha", "1.0xyz"},
            InvalidPositiveFiniteFloatTestCase{"TrailingSpace", "1.0 "},
            InvalidPositiveFiniteFloatTestCase{"IncompleteScientific", "1.0e"},
            InvalidPositiveFiniteFloatTestCase{"ExponentOverflowsFloat", "1e40"},
            InvalidPositiveFiniteFloatTestCase{"InfinityLiteral", "inf"},
            InvalidPositiveFiniteFloatTestCase{"NanLiteral", "nan"},
            InvalidPositiveFiniteFloatTestCase{"Zero", "0"},
            InvalidPositiveFiniteFloatTestCase{"ZeroDecimal", "0.0"},
            InvalidPositiveFiniteFloatTestCase{"NegativeInteger", "-1"},
            InvalidPositiveFiniteFloatTestCase{"NegativeDecimal", "-1.0"},
            InvalidPositiveFiniteFloatTestCase{"NegativeScientific", "-1e3"},
        };
    }
}  // namespace

class PositiveFiniteFloatRejectionTest : public ::testing::TestWithParam<InvalidPositiveFiniteFloatTestCase> {};

/**
 * @brief Inputs that violate the validator's contract round-trip to a diagnostic.
 *
 * One named case per distinct failure class so a regression in any single gate is
 * attributable from the case name alone, with cases grouped by which of the validator's
 * two diagnostics they trigger.
 */
TEST_P(PositiveFiniteFloatRejectionTest, ReturnsDiagnostic) {
    const auto& testCase{GetParam()};
    EXPECT_NE(runValidator(PositiveFiniteFloat, std::string{testCase.input}), "");
}

INSTANTIATE_TEST_SUITE_P(MalformedInputMatrix, PositiveFiniteFloatRejectionTest,
                         ::testing::ValuesIn(makeInvalidPositiveFiniteFloatTestCases()),
                         [](const ::testing::TestParamInfo<InvalidPositiveFiniteFloatTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct ValidFrequencyHzValidatorTestCase {
        std::string_view name;
        std::string_view input;
    };

    /**
     * @brief Render a ValidFrequencyHzValidatorTestCase as its name plus the accepted
     *        input for gtest listings and failure messages.
     */
    void PrintTo(const ValidFrequencyHzValidatorTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {input=\"" << testCase.input << "\"}";
    }

    /**
     * @brief A representative subset of parseFrequencyHz's accept axis - one named
     *        case per distinct parser admit class: integer Hz, scientific Hz, decimal-
     *        mantissa scientific resolving to integer Hz, and magnitudes across the
     *        megahertz / gigahertz range.
     */
    std::vector<ValidFrequencyHzValidatorTestCase> makeValidFrequencyHzValidatorTestCases() {
        return {
            ValidFrequencyHzValidatorTestCase{"IntegerHertz", "1"},
            ValidFrequencyHzValidatorTestCase{"IntegerHundredMegahertz", "100000000"},
            ValidFrequencyHzValidatorTestCase{"ScientificMegahertz", "100e6"},
            ValidFrequencyHzValidatorTestCase{"DecimalMantissaIntegerHz", "100.5e6"},
            ValidFrequencyHzValidatorTestCase{"GigahertzScientific", "1.5e9"},
        };
    }
}  // namespace

class FrequencyHzValidatorTest : public ::testing::TestWithParam<ValidFrequencyHzValidatorTestCase> {};

/**
 * @brief Valid frequency text resolves to an empty diagnostic.
 *
 * The FrequencyHz validator delegates to parseFrequencyHz, so the input matrix here is
 * a representative subset of the parser's accept axis - exhaustive parser-level
 * coverage lives in test_parse_frequency.cpp. The cases span integer notation,
 * scientific notation, and decimal-mantissa scientific resolving to integer Hz,
 * across magnitudes from 1 Hz to 1.5 GHz, verifying that each parser-level admit
 * class survives the wrapper's std::nullopt-to-empty-string translation.
 */
TEST_P(FrequencyHzValidatorTest, ReturnsEmptyDiagnostic) {
    const auto& testCase{GetParam()};
    EXPECT_EQ(runValidator(FrequencyHz, std::string{testCase.input}), "");
}

INSTANTIATE_TEST_SUITE_P(ValidInputMatrix, FrequencyHzValidatorTest,
                         ::testing::ValuesIn(makeValidFrequencyHzValidatorTestCases()),
                         [](const ::testing::TestParamInfo<ValidFrequencyHzValidatorTestCase>& info) {
                             return std::string{info.param.name};
                         });

namespace {
    struct InvalidFrequencyHzValidatorTestCase {
        std::string_view name;
        std::string_view input;
    };

    /**
     * @brief Render an InvalidFrequencyHzValidatorTestCase as its name plus the rejected
     *        input for gtest listings and failure messages.
     */
    void PrintTo(const InvalidFrequencyHzValidatorTestCase& testCase, std::ostream* os) {
        *os << testCase.name << " {input=\"" << testCase.input << "\"}";
    }

    /**
     * @brief One named case per distinct parser rejection class the wrapper must
     *        surface as a diagnostic, ordered by which parser gate they trip:
     *        truly-empty text; whitespace-only and wholly non-numeric input;
     *        exponents beyond double's representable range; trailing unit suffix;
     *        IEEE-754 inf/nan; non-positive magnitudes; 2^64 overflow; and
     *        fractional Hz.
     */
    std::vector<InvalidFrequencyHzValidatorTestCase> makeInvalidFrequencyHzValidatorTestCases() {
        return {
            InvalidFrequencyHzValidatorTestCase{"Empty", ""},
            InvalidFrequencyHzValidatorTestCase{"Whitespace", " "},
            InvalidFrequencyHzValidatorTestCase{"NonNumeric", "abc"},
            InvalidFrequencyHzValidatorTestCase{"ExponentOverflowsDouble", "1e999"},
            InvalidFrequencyHzValidatorTestCase{"TrailingUnit", "100Hz"},
            InvalidFrequencyHzValidatorTestCase{"InfinityLiteral", "inf"},
            InvalidFrequencyHzValidatorTestCase{"NanLiteral", "nan"},
            InvalidFrequencyHzValidatorTestCase{"NegativeInteger", "-1"},
            InvalidFrequencyHzValidatorTestCase{"Zero", "0"},
            InvalidFrequencyHzValidatorTestCase{"AboveTwoToTheSixtyFour", "1e20"},
            InvalidFrequencyHzValidatorTestCase{"FractionalHzDecimal", "100.5"},
        };
    }
}  // namespace

class FrequencyHzValidatorRejectionTest : public ::testing::TestWithParam<InvalidFrequencyHzValidatorTestCase> {};

/**
 * @brief Inputs the parser rejects round-trip through the wrapper to a diagnostic.
 *
 * The FrequencyHz wrapper collapses every parseFrequencyHz nullopt return into a single
 * diagnostic, so the matrix carries one named case per distinct parser rejection class.
 * Exhaustive coverage with multiple cases per gate lives in test_parse_frequency.cpp;
 * here this fan-out suffices because the wrapper has a single nullopt-branch.
 */
TEST_P(FrequencyHzValidatorRejectionTest, ReturnsDiagnostic) {
    const auto& testCase{GetParam()};
    EXPECT_NE(runValidator(FrequencyHz, std::string{testCase.input}), "");
}

INSTANTIATE_TEST_SUITE_P(MalformedInputMatrix, FrequencyHzValidatorRejectionTest,
                         ::testing::ValuesIn(makeInvalidFrequencyHzValidatorTestCases()),
                         [](const ::testing::TestParamInfo<InvalidFrequencyHzValidatorTestCase>& info) {
                             return std::string{info.param.name};
                         });
