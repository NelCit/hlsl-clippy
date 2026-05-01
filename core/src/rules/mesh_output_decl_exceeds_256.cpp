// mesh-output-decl-exceeds-256
//
// Detects mesh-shader entry points whose `out vertices` or `out indices`
// array declarations exceed 256 elements. The D3D12 mesh-pipeline spec caps
// both at 256; values above the cap fail PSO creation.
//
// Detection plan: AST. Walk the source for `out vertices ... arr[N]` and
// `out indices ... arr[N]` patterns. Constant-fold N and emit when N > 256.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "mesh-output-decl-exceeds-256";
constexpr std::string_view k_category = "mesh";
constexpr std::uint32_t k_cap = 256U;

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

void scan(std::string_view bytes, std::string_view kind, const AstTree& tree, RuleContext& ctx) {
    std::size_t pos = 0U;
    while (pos < bytes.size()) {
        const auto found = bytes.find(kind, pos);
        if (found == std::string_view::npos)
            return;
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        const std::size_t end = found + kind.size();
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        if (!ok_left || !ok_right) {
            pos = found + 1U;
            continue;
        }
        // Look forward for `[N]` (skip type + name).
        const auto lb = bytes.find('[', end);
        if (lb == std::string_view::npos) {
            pos = end;
            continue;
        }
        const auto rb = bytes.find(']', lb + 1U);
        if (rb == std::string_view::npos) {
            pos = end;
            continue;
        }
        // Stay on the same statement: bail if `;` appears between `kind` and
        // the `[`.
        if (bytes.substr(end, lb - end).find(';') != std::string_view::npos) {
            pos = end;
            continue;
        }
        const auto digits = trim(bytes.substr(lb + 1U, rb - lb - 1U));
        std::uint32_t v = 0U;
        bool ok = !digits.empty();
        for (const char c : digits) {
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
            v = v * 10U + static_cast<std::uint32_t>(c - '0');
        }
        if (!ok) {
            pos = rb + 1U;
            continue;
        }
        if (v > k_cap) {
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Error;
            diag.primary_span = Span{.source = tree.source_id(),
                                     .bytes = ByteSpan{static_cast<std::uint32_t>(found),
                                                       static_cast<std::uint32_t>(rb + 1U)}};
            diag.message = std::string{"`out "} + std::string{kind.substr(4U)} + " ...[" +
                           std::to_string(v) +
                           "]` exceeds the D3D12 mesh-shader cap of 256 -- PSO creation will "
                           "return `E_INVALIDARG`";
            ctx.emit(std::move(diag));
        }
        pos = rb + 1U;
    }
}

class MeshOutputDeclExceeds256 : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Ast;
    }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        const auto bytes = tree.source_bytes();
        scan(bytes, "out vertices", tree, ctx);
        scan(bytes, "out indices", tree, ctx);
        scan(bytes, "out primitives", tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_mesh_output_decl_exceeds_256() {
    return std::make_unique<MeshOutputDeclExceeds256>();
}

}  // namespace hlsl_clippy::rules
