// Internal bridge interface to Slang. The implementation in `slang_bridge.cpp`
// is the ONE AND ONLY translation unit anywhere under `core/` that includes
// `<slang.h>`. Every other file goes through this header (or through the
// engine on top of it) and never sees a Slang type.
//
// The bridge owns the process-singleton `IGlobalSession` (lazy-initialised on
// first use) and a per-instance pool of `ISession` workers sized at construct-
// time. Compile + reflection of one source produces a fully-materialised
// `ReflectionInfo` value that the caller can cache or hand directly to rules.

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::reflection {

/// PIMPL'd Slang-talking bridge. Construction is non-trivial (creates the
/// ISession pool); copy / move are deleted to keep the pool in one place.
class SlangBridge {
public:
    explicit SlangBridge(std::uint32_t pool_size);
    SlangBridge(const SlangBridge&) = delete;
    SlangBridge& operator=(const SlangBridge&) = delete;
    SlangBridge(SlangBridge&&) = delete;
    SlangBridge& operator=(SlangBridge&&) = delete;
    ~SlangBridge();

    /// Compile `source`'s contents through Slang and walk the resulting
    /// reflection tree into a `ReflectionInfo`. Returns
    /// `std::unexpected(Diagnostic{...})` when Slang reports a compile or
    /// reflection failure; the diagnostic carries `code = "clippy::reflection"`
    /// and `severity = Severity::Error`.
    [[nodiscard]] std::expected<ReflectionInfo, Diagnostic> reflect(
        const SourceManager& sources, SourceId source, std::string_view target_profile);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace shader_clippy::reflection
