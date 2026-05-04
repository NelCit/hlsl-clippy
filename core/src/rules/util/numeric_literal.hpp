// Shared numeric-literal predicates for AST-only rules.
//
// These helpers intentionally recognise only conservative decimal spellings:
// optional leading '+', decimal integer part, optional all-zero fractional
// part, and common HLSL scalar suffixes. Scientific notation is rejected so
// rules do not silently broaden their match surface.

#pragma once

#include <cstdint>
#include <string_view>

namespace shader_clippy::rules::util {

[[nodiscard]] bool is_numeric_literal_uint(std::string_view text, std::uint64_t value) noexcept;
[[nodiscard]] bool is_numeric_literal_zero(std::string_view text) noexcept;
[[nodiscard]] bool is_numeric_literal_one(std::string_view text) noexcept;
[[nodiscard]] bool is_numeric_literal_two(std::string_view text) noexcept;
[[nodiscard]] bool is_numeric_literal_255(std::string_view text) noexcept;

}  // namespace shader_clippy::rules::util
