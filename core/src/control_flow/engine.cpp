// CfgEngine implementation -- glues the builder + dominator pass +
// uniformity analyzer together behind a single `build()` entry point.
// Caches the result per `SourceId` so multiple control-flow-stage rules
// against the same source pay one walk per source per lint run.

#include "control_flow/engine.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "control_flow/cfg_builder.hpp"
#include "control_flow/cfg_storage.hpp"
#include "control_flow/dominators.hpp"
#include "control_flow/uniformity_analyzer.hpp"
#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"

namespace hlsl_clippy::control_flow {

namespace {

/// FNV-1a 64-bit hash of the source contents. Used to disambiguate cache
/// entries whose `SourceId.value` collides across SourceManagers.
[[nodiscard]] std::uint64_t fingerprint(std::string_view contents) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (const char c : contents) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= prime;
    }
    return hash;
}

}  // namespace

CfgEngine& CfgEngine::instance() noexcept {
    static CfgEngine engine;
    return engine;
}

std::expected<ControlFlowInfo, Diagnostic> CfgEngine::build(
    const SourceManager& sources,
    SourceId source,
    const ReflectionInfo* reflection_or_null,
    std::uint32_t cfg_inlining_depth) {
    auto parsed = parser::parse(sources, source);
    if (!parsed) {
        Diagnostic diag;
        diag.code = std::string{"clippy::cfg"};
        diag.severity = Severity::Error;
        diag.primary_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
        diag.message = std::string{"failed to parse source for CFG construction"};
        return std::unexpected{std::move(diag)};
    }
    const ::TSNode root = ::ts_tree_root_node(parsed->tree.get());
    return build_with_tree(source, root, parsed->bytes, reflection_or_null, cfg_inlining_depth);
}

std::expected<ControlFlowInfo, Diagnostic> CfgEngine::build_with_tree(
    SourceId source,
    ::TSNode root,
    std::string_view source_bytes,
    const ReflectionInfo* reflection_or_null,
    std::uint32_t cfg_inlining_depth) {
    const std::uint64_t fp = fingerprint(source_bytes);
    const CacheKey key{source.value, fp};
    {
        std::shared_lock<std::shared_mutex> read_lock(cache_mu_);
        const auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second->info;
        }
    }

    if (::ts_node_is_null(root)) {
        Diagnostic diag;
        diag.code = std::string{"clippy::cfg"};
        diag.severity = Severity::Error;
        diag.primary_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
        diag.message = std::string{"null tree-sitter root passed to CFG engine"};
        return std::unexpected{std::move(diag)};
    }

    auto build_result = build_cfg(root, source, source_bytes);
    if (build_result.storage == nullptr) {
        Diagnostic diag;
        diag.code = std::string{"clippy::cfg"};
        diag.severity = Severity::Error;
        diag.primary_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
        diag.message = std::string{"CFG builder returned no storage"};
        return std::unexpected{std::move(diag)};
    }

    compute_all_dominators(*build_result.storage);

    // Project storage into the public `CfgInfo`.
    auto cfg_impl = std::make_shared<CfgImpl>();
    cfg_impl->data.storage = build_result.storage;

    ControlFlowInfo info;
    info.cfg.impl = cfg_impl;
    info.cfg.blocks.reserve(build_result.storage->block_to_function.size());
    for (std::size_t i = 0; i < build_result.storage->block_to_function.size(); ++i) {
        info.cfg.blocks.emplace_back(static_cast<std::uint32_t>(i + 1U));
    }
    if (build_result.storage->functions.size() > 1U) {
        info.cfg.entry_span = build_result.storage->functions[1].declaration_span;
    } else {
        info.cfg.entry_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
    }

    // Run uniformity over the same root.
    auto uni_impl = std::make_shared<UniformityImpl>();
    uni_impl->data.storage = build_result.storage;
    analyse_uniformity(
        root, source, source_bytes, reflection_or_null, cfg_inlining_depth, uni_impl->data);
    info.uniformity.impl = uni_impl;

    auto entry = std::make_shared<Entry>();
    entry->info = info;
    entry->diagnostics = std::move(build_result.diagnostics);

    {
        std::unique_lock<std::shared_mutex> write_lock(cache_mu_);
        const auto inserted = cache_.try_emplace(key, entry);
        return inserted.first->second->info;
    }
}

void CfgEngine::clear_cache() {
    std::unique_lock<std::shared_mutex> write_lock(cache_mu_);
    cache_.clear();
}

std::vector<Diagnostic> CfgEngine::take_diagnostics(SourceId source) {
    // The diagnostics were stored under a (source.value, fingerprint) key,
    // but `take_diagnostics` only knows the SourceId. Look up by source.value
    // and return the first matching entry's diagnostics. There is at most
    // one CFG entry per source per process under typical use; tests with
    // colliding source.values across distinct SourceManagers will see only
    // the most recent entry's diagnostics, which is acceptable.
    std::shared_lock<std::shared_mutex> read_lock(cache_mu_);
    for (const auto& [key, entry] : cache_) {
        if (key.first == source.value) {
            return entry->diagnostics;
        }
    }
    return {};
}

}  // namespace hlsl_clippy::control_flow
