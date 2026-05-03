#pragma once

#include <string_view>

namespace shader_clippy {

/// Returns the current version string of shader-clippy.
[[nodiscard]] std::string_view version() noexcept;

}  // namespace shader_clippy
