// nodeid-implicit-mismatch
//
// Detects work-graph `NodeOutput<T>` declarations that lack an explicit
// `[NodeId("...")]` annotation. Without the annotation the output's downstream
// node id is inferred from the struct / output identifier, and a rename of
// the struct can silently break the graph wiring.
//
// Detection plan: AST. Walk the source for `NodeOutput<` and
// `NodeOutputArray<` template-id headers. For each, look backwards for an
// `[NodeId(...)]` attribute on the same parameter / declaration. Emit when
// missing.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::is_id_char;

constexpr std::string_view k_rule_id = "nodeid-implicit-mismatch";
constexpr std::string_view k_category = "work-graphs";

class NodeIdImplicitMismatch : public Rule {
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
        for (const std::string_view needle :
             {"NodeOutput<", "NodeOutputArray<", "EmptyNodeOutput<", "EmptyNodeOutputArray<"}) {
            std::size_t pos = 0U;
            while (pos < bytes.size()) {
                const auto found = bytes.find(needle, pos);
                if (found == std::string_view::npos)
                    break;
                const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
                if (!ok_left) {
                    pos = found + 1U;
                    continue;
                }
                // Walk back ~256 bytes looking for `[NodeId`. Bail at `(`
                // / `;` / `}` -- those mark a different declaration.
                bool found_attr = false;
                const std::size_t scan_lo = (found > 256U) ? (found - 256U) : 0U;
                for (std::size_t k = found; k > scan_lo; --k) {
                    if (bytes.size() - k >= 7U && bytes.substr(k, 7U) == "[NodeId") {
                        found_attr = true;
                        break;
                    }
                    if (bytes[k] == ';' || bytes[k] == '}')
                        break;
                }
                if (!found_attr) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Note;
                    diag.primary_span =
                        Span{.source = tree.source_id(),
                             .bytes = ByteSpan{static_cast<std::uint32_t>(found),
                                               static_cast<std::uint32_t>(found + needle.size())}};
                    diag.message = std::string{"`"} +
                                   std::string{needle.substr(0, needle.size() - 1U)} +
                                   "` declaration without an explicit `[NodeId(\"...\")]` -- the "
                                   "downstream node id is inferred from the struct identifier and "
                                   "a rename can silently break work-graph wiring";
                    ctx.emit(std::move(diag));
                }
                pos = found + needle.size();
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_nodeid_implicit_mismatch() {
    return std::make_unique<NodeIdImplicitMismatch>();
}

}  // namespace shader_clippy::rules
