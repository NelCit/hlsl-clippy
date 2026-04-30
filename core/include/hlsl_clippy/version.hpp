#pragma once

#include <string_view>

namespace hlsl_clippy {

/// Returns the current version string of hlsl-clippy.
[[nodiscard]] std::string_view version() noexcept;

}  // namespace hlsl_clippy
