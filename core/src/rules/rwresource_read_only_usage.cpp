// rwresource-read-only-usage
//
// Detects `RWBuffer` / `RWTexture*` / `RWStructuredBuffer` /
// `RWByteAddressBuffer` bindings that are only ever read in the source --
// every appearance of the resource name is followed by a read-pattern token
// (`[...]` for an indexed read, `.Load(...)`, `.SampleLevel(...)`, etc.) and
// never by an assignment-LHS index pattern (`<name>[...] = ...` or a `.Store`
// method call). When a resource is read-only it should be declared as the
// matching SRV (`Buffer`, `Texture*`, `StructuredBuffer`, `ByteAddressBuffer`)
// to free a UAV slot and let the driver bind it through the texture cache
// path.
//
// Detection plan: for every reflection binding whose `kind` is in the writable
// set (`util::is_writable`), search the source bytes for the binding's name
// as a standalone identifier. For each occurrence, look at the first
// non-whitespace token after the name's tail; if any occurrence shows a write
// shape (`name[...] =` or `.Store` / `.IncrementCounter` / `.DecrementCounter`
// / `.Append` / `.Consume`), the resource is written -- skip. Otherwise, emit.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/reflect_resource.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::is_id_char;

constexpr std::string_view k_rule_id = "rwresource-read-only-usage";
constexpr std::string_view k_category = "bindings";

/// Skip over a balanced `[...]` block starting at `pos` (which must point at
/// `[`). Returns the index just past the matching `]`, or `npos` on
/// malformed.
[[nodiscard]] std::size_t skip_brackets(std::string_view bytes, std::size_t pos) noexcept {
    if (pos >= bytes.size() || bytes[pos] != '[')
        return std::string_view::npos;
    int depth = 0;
    while (pos < bytes.size()) {
        const char c = bytes[pos];
        if (c == '[')
            ++depth;
        else if (c == ']') {
            --depth;
            if (depth == 0)
                return pos + 1U;
        }
        ++pos;
    }
    return std::string_view::npos;
}

/// True when the byte sequence starting at `tail` looks like a write to the
/// resource: `[...] =` (skipping whitespace) or one of the writable HLSL
/// methods (`.Store*`, `.Append`, `.Consume`, `.IncrementCounter`,
/// `.DecrementCounter`).
[[nodiscard]] bool tail_looks_like_write(std::string_view bytes, std::size_t tail) noexcept {
    // Skip whitespace.
    while (tail < bytes.size() && (bytes[tail] == ' ' || bytes[tail] == '\t' ||
                                   bytes[tail] == '\n' || bytes[tail] == '\r'))
        ++tail;
    if (tail >= bytes.size())
        return false;
    if (bytes[tail] == '[') {
        const auto past = skip_brackets(bytes, tail);
        if (past == std::string_view::npos)
            return false;
        std::size_t i = past;
        while (i < bytes.size() &&
               (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\n' || bytes[i] == '\r'))
            ++i;
        if (i >= bytes.size())
            return false;
        // Skip swizzles like `.x` / `.xyz` after `[i]`.
        if (bytes[i] == '.') {
            ++i;
            while (i < bytes.size() && is_id_char(bytes[i]))
                ++i;
            while (i < bytes.size() &&
                   (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\n' || bytes[i] == '\r'))
                ++i;
        }
        // Detect `=` (but not `==`).
        return i < bytes.size() && bytes[i] == '=' &&
               (i + 1U >= bytes.size() || bytes[i + 1U] != '=');
    }
    if (bytes[tail] == '.') {
        const std::size_t name_start = tail + 1U;
        std::size_t i = name_start;
        while (i < bytes.size() && is_id_char(bytes[i]))
            ++i;
        const auto method = bytes.substr(name_start, i - name_start);
        return method == "Store" || method == "Store2" || method == "Store3" ||
               method == "Store4" || method == "InterlockedAdd" || method == "InterlockedAnd" ||
               method == "InterlockedOr" || method == "InterlockedXor" ||
               method == "InterlockedMin" || method == "InterlockedMax" ||
               method == "InterlockedExchange" || method == "InterlockedCompareExchange" ||
               method == "InterlockedCompareStore" || method == "Append" || method == "Consume" ||
               method == "IncrementCounter" || method == "DecrementCounter";
    }
    return false;
}

class RWResourceReadOnlyUsage : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Reflection;
    }

    void on_reflection(const AstTree& tree,
                       const ReflectionInfo& reflection,
                       RuleContext& ctx) override {
        const auto bytes = tree.source_bytes();
        for (const auto& binding : reflection.bindings) {
            if (!util::is_writable(binding.kind))
                continue;
            // Append/Consume buffers are queue-shaped; treat them as in-use
            // even on read-only methods because the binding type itself
            // signals intent.
            if (binding.kind == ResourceKind::AppendStructuredBuffer ||
                binding.kind == ResourceKind::ConsumeStructuredBuffer)
                continue;
            const auto& name = binding.name;
            if (name.empty())
                continue;
            bool any_use = false;
            bool any_write = false;
            std::size_t pos = 0U;
            while (pos <= bytes.size()) {
                const auto found = bytes.find(name, pos);
                if (found == std::string_view::npos)
                    break;
                const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
                const std::size_t end = found + name.size();
                const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
                if (ok_left && ok_right) {
                    // Skip the declaration span itself.
                    const auto abs = static_cast<std::uint32_t>(found);
                    const bool in_decl = (abs >= binding.declaration_span.bytes.lo &&
                                          abs < binding.declaration_span.bytes.hi);
                    if (!in_decl) {
                        any_use = true;
                        if (tail_looks_like_write(bytes, end)) {
                            any_write = true;
                            break;
                        }
                    }
                }
                pos = found + 1U;
            }
            if (!any_use || any_write)
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = binding.declaration_span;
            diag.message = std::string{"`"} + name +
                           "` is declared as a writable resource but is only ever read -- "
                           "demote to the matching SRV (Buffer / Texture* / StructuredBuffer / "
                           "ByteAddressBuffer) to free a UAV slot and use the texture-cache path";
            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_rwresource_read_only_usage() {
    return std::make_unique<RWResourceReadOnlyUsage>();
}

}  // namespace shader_clippy::rules
