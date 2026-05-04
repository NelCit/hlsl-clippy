#include "rules/util/numeric_literal.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace shader_clippy::rules::util {

namespace {

[[nodiscard]] bool is_literal_suffix_char(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L' ||
           c == 'u' || c == 'U';
}

}  // namespace

bool is_numeric_literal_uint(std::string_view text, std::uint64_t value) noexcept {
    if (text.empty()) {
        return false;
    }

    std::size_t i = 0;
    if (text[i] == '+') {
        ++i;
    }

    const std::size_t int_begin = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        ++i;
    }
    if (i == int_begin) {
        return false;
    }

    const auto int_part = text.substr(int_begin, i - int_begin);
    std::size_t significant = 0;
    while (significant < int_part.size() && int_part[significant] == '0') {
        ++significant;
    }

    if (value == 0U) {
        if (significant != int_part.size()) {
            return false;
        }
    } else {
        if (significant >= int_part.size()) {
            return false;
        }
        if (int_part.substr(significant) != std::to_string(value)) {
            return false;
        }
    }

    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            if (text[i] != '0') {
                return false;
            }
            ++i;
        }
    }

    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        return false;
    }

    while (i < text.size()) {
        if (!is_literal_suffix_char(text[i])) {
            return false;
        }
        ++i;
    }
    return true;
}

bool is_numeric_literal_zero(std::string_view text) noexcept {
    return is_numeric_literal_uint(text, 0U);
}

bool is_numeric_literal_one(std::string_view text) noexcept {
    return is_numeric_literal_uint(text, 1U);
}

bool is_numeric_literal_two(std::string_view text) noexcept {
    return is_numeric_literal_uint(text, 2U);
}

bool is_numeric_literal_255(std::string_view text) noexcept {
    return is_numeric_literal_uint(text, 255U);
}

}  // namespace shader_clippy::rules::util
