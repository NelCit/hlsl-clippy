<!--
date: 2026-04-30
prompt-summary: pick the going-forward license for hlsl-clippy code, test corpus, fixtures, and documentation; weigh CLA vs DCO; assess naming / branding / trademark risk; recommend distribution-surface and marketing posture.
preserved-verbatim: yes — see ../0006-license-apache-2-0.md for the distilled decision.
follow-up: the LICENSE file at the repo root currently still shows MIT. Replacing it with the verbatim Apache-2.0 text is a separate task tracked in ROADMAP.md "Licensing".
-->

# hlsl-clippy license + branding recommendation

Primary recommendation: Apache License 2.0 for the code, CC-BY-4.0 for prose, project license for fixtures, DCO for contributions.

## 1. Best license for the project itself

Apache-2.0 wins on three vectors:
- Compatibility: matches Slang upstream (Apache-2.0 + LLVM exception); MIT and BSD vendored deps are one-way compatible.
- Patent grant with retaliation clause — matters because we vendor a shader compiler walking through GPU-compilation patent territory; downstream studios/IHVs have legal teams who notice this.
- Adoption-friction-free at every AAA studio + IHV. UE itself ships Apache-2.0 deps; Unity ships Apache-2.0 deps; LLVM is Apache-2.0 + LLVM exception.

Reputation signal at NVIDIA/AMD/Epic/Valve: picking Apache-2.0 says you've read CONTRIBUTING.md at three large projects, you understand patent grants. MIT is fine but slightly weaker. AGPL is a negative credential for graphics-tools hire. Dual MIT/Apache (Rust convention) signals "Rust person" more than "graphics person."

Do NOT pick AGPL/EUPL/MPL.

## 2. License for the test corpus and shader fixtures

- tests/fixtures/ (hand-written): Apache-2.0 (project license).
- tests/corpus/ (third-party): each file keeps upstream license. Maintain tests/corpus/SOURCES.md. CC-BY shaders stay in repo; never bake into binary unless attribution discipline is wired up.

## 3. License for documentation, blog posts, rule pages

CC-BY-4.0. Standard for technical writing wanting maximum reach with attribution preserved (Mozilla MDN, Khronos, academic preprints). NOT CC-BY-SA-4.0 (share-alike blocks commercial reuse — wrong choice for a reputation play).

Concrete: docs/ and rule-catalog pages CC-BY-4.0 footer "© 2026 NelCit, CC-BY-4.0." Code snippets inside docs under project Apache-2.0.

## 4. LICENSE and NOTICE file structure

- LICENSE: verbatim Apache-2.0 text, unmodified.
- NOTICE: short attribution-only paragraph + per-vendored-dep one-liners (Slang Apache-2.0+LLVM-exception; tree-sitter MIT; tree-sitter-hlsl MIT-pending-verify; Microsoft GSL MIT).
- THIRD_PARTY_LICENSES.md: full text of each vendored dep license, sectioned. Ships inside binary releases.

Verify tree-sitter-hlsl license before vendoring (most TS grammars are MIT but some are Apache or unlicensed).

## 5. CLA vs DCO

DCO. Signed-off-by on every commit, enforced via dco GitHub App or 10-line workflow. CLA introduces friction for a solo project chasing adoption. Linux kernel, GitLab, CNCF moved to DCO.

For "Anthropic / IHV interest later" growth, DCO is the safer reputation move. CLAs read as "preparing to monetize and lock the door."

## 6. Trademark, naming, branding

Risk: low. Don't change the name. HLSL is descriptive; tools like dxc, glslang, naga coexist. Clippy-as-meme has rust-clippy precedent (2014, no Microsoft action). hlsl-clippy is homage, not impersonation.

Don't register a trademark now (USPTO ~$350/class + attorney + premature for pre-v0). Keep the name.

## 7. Distribution surface

- GitHub Releases binaries: no crypto, no encryption-controlled categories. No EAR ECCN signaling needed.
- VS Code Marketplace: Apache-2.0 fully compatible; license link mandatory in package.json.
- Linting-as-a-service / hosted CI gate: Apache-2.0 imposes no SaaS obligations. Permissive maximizes adoption trust.

## 8. Marketing signal

README badges: build · license: Apache-2.0 · version · sponsors. Skip "PRs welcome" badge.

"Why this license?" blurb in CONTRIBUTING.md (paraphrase): hlsl-clippy is Apache-2.0 because Slang is Apache-2.0, because patent grants matter for tools in GPU-compilation territory, because Apache-2.0 is the friction-free choice for game-engine and IHV consumers. Contributions accepted under DCO — no CLA.

Funding: GitHub Sponsors primary. Open Collective optional. Skip Patreon (off-brand for dev tool).
