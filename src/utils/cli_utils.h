/**
 * @file cli_utils.h
 * @brief Command-line argument parsing utilities shared across rpitx-ui tools.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 15.04.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

/**
 * @brief Types accepted by parseNumericArg / assignNumericFlag.
 *
 * Matches what std::from_chars supports (integrals and floating-points)
 * and makes the constraint explicit at the signature level, so misuse
 * produces a clean "constraint not satisfied" diagnostic instead of a
 * deep overload-resolution error inside <charconv>.
 */
template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

/**
 * @brief Parse a numeric CLI argument via std::from_chars.
 *
 * Strict parsing: the entire input must be consumed and no trailing characters
 * are permitted. Locale-independent and allocation-free (unlike std::stoi
 * / std::stof), and reports failure via std::optional instead of exceptions.
 *
 * @tparam T Target numeric type (integer or floating-point).
 * @param arg Input argument.
 * @return Parsed value on success, std::nullopt on error.
 */
template <Numeric T>
[[nodiscard]] std::optional<T> parseNumericArg(std::string_view arg) {
    T value{};
    const auto first{arg.data()};
    const auto last{arg.data() + arg.size()};
    if (const auto [ptr, ec]{std::from_chars(first, last, value)}; ec != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return value;
}

/**
 * @brief Row of a textual-name / enum-value lookup table.
 *
 * Used together with parseNamedEnum() and formatNamedEnum() to declare the
 * name/value mapping of a CLI-facing enum in one place.
 *
 * @tparam Enum Enum type being named.
 */
template <typename Enum>
struct NamedEnum {
    std::string_view name;
    Enum value;
};

/**
 * @brief Look up an enum value by its textual name in a compile-time table.
 *
 * @tparam Enum Enum type.
 * @tparam N    Number of rows in the table (deduced).
 * @param name  Textual name to match against NamedEnum::name.
 * @param table Table of name/value pairs.
 * @return Matching enum value on hit, std::nullopt on miss.
 */
template <typename Enum, std::size_t N>
[[nodiscard]] std::optional<Enum> parseNamedEnum(std::string_view name, const std::array<NamedEnum<Enum>, N>& table) {
    const auto it{std::ranges::find(table, name, &NamedEnum<Enum>::name)};
    if (it != table.end()) {
        return it->value;
    }
    return std::nullopt;
}

/**
 * @brief Reverse lookup: textual name of an enum value in a compile-time table.
 *
 * @tparam Enum    Enum type.
 * @tparam N       Number of rows in the table (deduced).
 * @param value    Enum value to stringify.
 * @param table    Table of name/value pairs.
 * @param fallback Returned when value is not present in the table.
 * @return Name on hit, fallback on miss.
 */
template <typename Enum, std::size_t N>
[[nodiscard]] std::string_view formatNamedEnum(Enum value, const std::array<NamedEnum<Enum>, N>& table,
                                               std::string_view fallback = "unknown") {
    const auto it{std::ranges::find(table, value, &NamedEnum<Enum>::value)};
    if (it != table.end()) {
        return it->name;
    }
    return fallback;
}

/**
 * @brief Outcome of command-line argument parsing shared across rpitx-ui tools.
 *
 * Distinguishes a successful parse, a user-visible error, and a help request
 * so that the caller can exit with a proper status code (0 for Help, non-zero
 * for Error) without extra sentinels.
 */
enum class ParseResult {
    Ok,     ///< Options successfully populated.
    Error,  ///< Invalid arguments; caller should exit with non-zero code.
    Help,   ///< User requested help (e.g. -h); caller should exit cleanly.
};

/**
 * @brief Check whether a given flag string appears anywhere in the argument slice.
 *
 * Intended for pre-scans over argv of boolean/presence-only flags such as -h
 * that should short-circuit parsing regardless of their position or of any
 * positional-count gate. Matching is exact-equal on the whole token - no
 * prefix / substring behaviour.
 *
 * @param args Slice of argv to scan (typically `{argv + 1, argv + argc}`).
 * @param flag Flag string to look for (e.g. "-h").
 * @return true if any element of args equals flag, false otherwise.
 */
[[nodiscard]] bool containsFlag(std::span<char* const> args, std::string_view flag);

/**
 * @brief Print a uniform "[ERROR] Invalid <what>: '<value>'" diagnostic and
 *        return ParseResult::Error.
 *
 * Keeps per-flag parsing blocks focused on the happy path and ensures all
 * CLI value-rejection messages across rpitx-ui tools share the same format.
 *
 * Defined out-of-line in cli_utils.cpp so that this header does not have to
 * pull in <iostream> for every translation unit that uses the parsing helpers.
 *
 * @note Declared before assignNumericFlag() so that ordinary (non-dependent)
 *       name lookup at the template definition point resolves correctly under
 *       C++ two-phase lookup (GCC/Clang enforce this strictly).
 *
 * @param what  Human-readable field description (e.g. "tone count", "sample rate").
 * @param value The offending textual value (echoed back to help the user).
 * @return Always ParseResult::Error - intended for use as `return reportInvalidValue(...);`.
 */
[[nodiscard]] ParseResult reportInvalidValue(std::string_view what, std::string_view value);

/**
 * @brief Deduce the numeric parse type from an assignNumericFlag output field.
 *
 * Primary template is intentionally undefined - instantiation fails for any
 * field type that is neither a Numeric nor a std::optional of a Numeric, which
 * is what we want: mis-typed fields should not silently pick a conversion.
 */
namespace numeric_traits {
    template <typename>
    struct NumericFieldType;

    template <Numeric T>
    struct NumericFieldType<T> {
        using type = T;
    };

    template <Numeric T>
    struct NumericFieldType<std::optional<T>> {
        using type = T;
    };
}  // namespace numeric_traits

/**
 * @brief Parse a numeric CLI value and assign it to an output field.
 *
 * Collapses the repeating shape seen in option parsers:
 *   1. parse via parseNumericArg,
 *   2. on failure emit the uniform diagnostic via reportInvalidValue,
 *   3. on success store into the caller-provided field.
 *
 * Field may be a bare Numeric T or a std::optional<Numeric T>. The optional
 * form lets callers model "flag was / was not supplied" directly in the type
 * system instead of reserving a magic sentinel value; on success the optional
 * becomes engaged with the parsed value. The from_chars parse type is deduced
 * from Field via numeric_traits::NumericFieldType so both call shapes are identical.
 *
 * @tparam Field Output field type - Numeric T or std::optional<Numeric T>.
 * @param value  Raw textual value from the command line.
 * @param what   Human-readable field description used in the error message
 *               (e.g. "tone count", "sample rate").
 * @param field  Output - assigned the parsed value on success, untouched on failure.
 * @return ParseResult::Ok on success, ::Error on parse failure (diagnostic
 *         already printed).
 */
template <typename Field>
[[nodiscard]] ParseResult assignNumericFlag(std::string_view value, std::string_view what, Field& field) {
    using T = typename numeric_traits::NumericFieldType<Field>::type;
    const auto parsed{parseNumericArg<T>(value)};
    if (parsed == std::nullopt) {
        return reportInvalidValue(what, value);
    }
    field = parsed.value();
    return ParseResult::Ok;
}
