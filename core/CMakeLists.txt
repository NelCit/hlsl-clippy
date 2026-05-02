add_library(hlsl_clippy_core STATIC
    src/version.cpp
    src/source.cpp
    src/parser.cpp
    src/lint.cpp
    src/rule_context.cpp
    src/rewriter.cpp
    src/suppress.cpp
    src/config.cpp
    src/query/query.cpp
    src/rules/registry.cpp
    src/rules/pow_const_squared.cpp
    src/rules/redundant_saturate.cpp
    src/rules/clamp01_to_saturate.cpp
    src/rules/lerp_extremes.cpp
    src/rules/mul_identity.cpp
    src/rules/sin_cos_pair.cpp
    src/rules/manual_reflect.cpp
    src/rules/manual_refract.cpp
    src/rules/manual_step.cpp
    src/rules/manual_smoothstep.cpp

    # Phase 2 — saturate-redundancy + bit-manipulation rules.
    src/rules/redundant_normalize.cpp
    src/rules/redundant_transpose.cpp
    src/rules/redundant_abs.cpp
    src/rules/countbits_vs_manual_popcount.cpp
    src/rules/firstbit_vs_log2_trick.cpp
    src/rules/manual_mad_decomposition.cpp

    # Phase 2 — pow + vec/length rules.
    src/rules/pow_to_mul.cpp
    src/rules/pow_base_two_to_exp2.cpp
    src/rules/pow_integer_decomposition.cpp
    src/rules/inv_sqrt_to_rsqrt.cpp
    src/rules/manual_distance.cpp
    src/rules/length_comparison.cpp
    src/rules/length_then_divide.cpp
    src/rules/dot_on_axis_aligned_vector.cpp

    # Phase 2 — misc / numerical safety rules.
    src/rules/compare_equal_float.cpp
    src/rules/comparison_with_nan_literal.cpp
    src/rules/redundant_precision_cast.cpp

    # Phase 2 — finalize pack (cross-with-up-vector + ADR 0011 Phase 2 candidates).
    src/rules/cross_with_up_vector.cpp
    src/rules/groupshared_volatile.cpp
    src/rules/lerp_on_bool_cond.cpp
    src/rules/select_vs_lerp_of_constant.cpp
    src/rules/redundant_unorm_snorm_conversion.cpp
    src/rules/wavereadlaneat_constant_zero_to_readfirst.cpp
    src/rules/loop_attribute_conflict.cpp

    # Phase 3 — Pack A (ADR 0011 buffers + groupshared-typed).
    src/rules/byteaddressbuffer_load_misaligned.cpp
    src/rules/byteaddressbuffer_narrow_when_typed_fits.cpp
    src/rules/groupshared_16bit_unpacked.cpp
    src/rules/groupshared_union_aliased.cpp
    src/rules/structured_buffer_stride_not_cache_aligned.cpp

    # Phase 3 — Pack B (ADR 0011 samplers + texture-format).
    src/rules/anisotropy_without_anisotropic_filter.cpp
    src/rules/bgra_rgba_swizzle_mismatch.cpp
    src/rules/comparison_sampler_without_comparison_op.cpp
    src/rules/manual_srgb_conversion.cpp
    src/rules/mip_clamp_zero_on_mipped_texture.cpp
    src/rules/sampler_feedback_without_streaming_flag.cpp
    src/rules/static_sampler_when_dynamic_used.cpp

    # Phase 3 — Pack C (ADR 0011 root-sig + compute + wave + state).
    src/rules/cbuffer_large_fits_rootcbv_not_table.cpp
    src/rules/compute_dispatch_grid_shape_vs_quad.cpp
    src/rules/uav_srv_implicit_transition_assumed.cpp
    src/rules/wavereadlaneat_constant_non_zero_portability.cpp
    src/rules/wavesize_attribute_missing.cpp

    # Phase 3 — Pack D (ADR 0007 §Phase 3 — bindings/texture/workgroup/etc).
    src/rules/all_resources_bound_not_set.cpp
    src/rules/as_payload_over_16k.cpp
    src/rules/bool_straddles_16b.cpp
    src/rules/cbuffer_fits_rootconstants.cpp
    src/rules/cbuffer_padding_hole.cpp
    src/rules/dead_store_sv_target.cpp
    src/rules/descriptor_heap_no_non_uniform_marker.cpp
    src/rules/descriptor_heap_type_confusion.cpp
    src/rules/excess_interpolators.cpp
    src/rules/feedback_write_wrong_stage.cpp
    src/rules/gather_channel_narrowing.cpp
    src/rules/gather_cmp_vs_manual_pcf.cpp
    src/rules/groupshared_too_large.cpp
    src/rules/mesh_numthreads_over_128.cpp
    src/rules/mesh_output_decl_exceeds_256.cpp
    src/rules/min16float_in_cbuffer_roundtrip.cpp
    src/rules/missing_precise_on_pcf.cpp
    src/rules/missing_ray_flag_cull_non_opaque.cpp
    src/rules/nodeid_implicit_mismatch.cpp
    src/rules/nointerpolation_mismatch.cpp
    src/rules/non_uniform_resource_index.cpp
    src/rules/numthreads_not_wave_aligned.cpp
    src/rules/numthreads_too_small.cpp
    src/rules/oversized_cbuffer.cpp
    src/rules/pack_clamp_on_prove_bounded.cpp
    src/rules/rov_without_earlydepthstencil.cpp
    src/rules/rwresource_read_only_usage.cpp
    src/rules/samplecmp_vs_manual_compare.cpp
    src/rules/samplegrad_with_constant_grads.cpp
    src/rules/samplelevel_with_zero_on_mipped_tex.cpp
    src/rules/structured_buffer_stride_mismatch.cpp
    src/rules/sv_depth_vs_conservative_depth.cpp
    src/rules/texture_array_known_slice_uniform.cpp
    src/rules/texture_as_buffer.cpp
    src/rules/texture_lod_bias_without_grad.cpp
    src/rules/unused_cbuffer_field.cpp
    src/rules/vrs_incompatible_output.cpp

    # Phase 3 — Pack E (ADR 0010 §Phase 3 — SM 6.9 SER / CoopVec / Long Vectors / OMM / Mesh Nodes).
    src/rules/coopvec_base_offset_misaligned.cpp
    src/rules/coopvec_fp8_with_non_optimal_layout.cpp
    src/rules/coopvec_non_optimal_matrix_layout.cpp
    src/rules/coopvec_non_uniform_matrix_handle.cpp
    src/rules/coopvec_stride_mismatch.cpp
    src/rules/coopvec_transpose_without_feature_check.cpp
    src/rules/fromrayquery_invoke_without_shader_table.cpp
    src/rules/hitobject_construct_outside_allowed_stages.cpp
    src/rules/hitobject_invoke_after_recursion_cap.cpp
    src/rules/hitobject_passed_to_non_inlined_fn.cpp
    src/rules/hitobject_stored_in_memory.cpp
    src/rules/long_vector_bytebuf_load_misaligned.cpp
    src/rules/long_vector_in_cbuffer_or_signature.cpp
    src/rules/long_vector_non_elementwise_intrinsic.cpp
    src/rules/long_vector_typed_buffer_load.cpp
    src/rules/maybereorderthread_outside_raygen.cpp
    src/rules/mesh_node_missing_output_topology.cpp
    src/rules/mesh_node_not_leaf.cpp
    src/rules/mesh_node_uses_vertex_shader_pipeline.cpp
    src/rules/omm_allocaterayquery2_non_const_flags.cpp
    src/rules/omm_rayquery_force_2state_without_allow_flag.cpp
    src/rules/omm_traceray_force_omm_2state_without_pipeline_flag.cpp
    src/rules/ser_trace_then_invoke_without_reorder.cpp

    # Phase 3 — reflection infrastructure (ADR 0012)
    src/reflection/reflection.cpp
    src/reflection/engine.cpp
    src/reflection/slang_bridge.cpp

    # Shared AST helpers used by every tree-sitter-driven rule TU
    # (factored out of ~95 anonymous-namespace duplicates).
    src/rules/util/ast_helpers.cpp

    # Phase 3 — shared utilities for reflection-aware rules (ADR 0012 sub-phase 3b).
    src/rules/util/reflect_resource.cpp
    src/rules/util/reflect_sampler.cpp
    src/rules/util/reflect_stage.cpp

    # Phase 4 — control-flow / data-flow infrastructure (ADR 0013 sub-phase 4a)
    src/control_flow/cfg_storage.cpp
    src/control_flow/cfg_builder.cpp
    src/control_flow/dominators.cpp
    src/control_flow/uniformity_analyzer.cpp
    src/control_flow/engine.cpp

    # Phase 4 — shared utilities for control-flow / data-flow rules (ADR 0013 sub-phase 4b).
    src/rules/util/cfg_query.cpp
    src/rules/util/uniformity.cpp
    src/rules/util/light_dataflow.cpp
    src/rules/util/helper_lane_analyzer.cpp

    # Phase 7 — liveness + register-pressure heuristic over the Phase 4 CFG
    # (ADR 0017 sub-phases 7b.1 + 7b.2).
    src/rules/util/liveness.cpp
    src/rules/util/register_pressure_ast.cpp

    # Phase 4 — Pack A (groupshared microarch).
    src/rules/groupshared_stride_non_32_bank_conflict.cpp
    src/rules/groupshared_dead_store.cpp
    src/rules/groupshared_overwrite_before_barrier.cpp
    src/rules/groupshared_atomic_replaceable_by_wave.cpp
    src/rules/groupshared_first_read_without_barrier.cpp

    # Phase 4 — Pack B (uniformity-aware bindings + mesh + numerical).
    src/rules/divergent_buffer_index_on_uniform_resource.cpp
    src/rules/rwbuffer_store_without_globallycoherent.cpp
    src/rules/primcount_overrun_in_conditional_cf.cpp
    src/rules/dispatchmesh_not_called.cpp
    src/rules/clip_from_non_uniform_cf.cpp
    src/rules/precise_missing_on_iterative_refine.cpp

    # Phase 4 — Pack C (wave + control-flow attributes).
    src/rules/manual_wave_reduction_pattern.cpp
    src/rules/quadany_quadall_opportunity.cpp
    src/rules/wave_prefix_sum_vs_scan_with_atomics.cpp
    src/rules/flatten_on_uniform_branch.cpp
    src/rules/forcecase_missing_on_ps_switch.cpp

    # Phase 4 — Pack D (ADR 0007 §Phase 4 — control-flow / data-flow rule set).
    src/rules/acos_without_saturate.cpp
    src/rules/barrier_in_divergent_cf.cpp
    src/rules/branch_on_uniform_missing_attribute.cpp
    src/rules/cbuffer_divergent_index.cpp
    src/rules/cbuffer_load_in_loop.cpp
    src/rules/derivative_in_divergent_cf.cpp
    src/rules/discard_then_work.cpp
    src/rules/div_without_epsilon.cpp
    src/rules/early_z_disabled_by_conditional_discard.cpp
    src/rules/groupshared_stride_32_bank_conflict.cpp
    src/rules/groupshared_uninitialized_read.cpp
    src/rules/groupshared_write_then_no_barrier_read.cpp
    src/rules/interlocked_bin_without_wave_prereduce.cpp
    src/rules/interlocked_float_bit_cast_trick.cpp
    src/rules/loop_invariant_sample.cpp
    src/rules/redundant_computation_in_branch.cpp
    src/rules/sample_in_loop_implicit_grad.cpp
    src/rules/small_loop_no_unroll.cpp
    src/rules/sqrt_of_potentially_negative.cpp
    src/rules/wave_active_all_equal_precheck.cpp
    src/rules/wave_intrinsic_helper_lane_hazard.cpp
    src/rules/wave_intrinsic_non_uniform.cpp

    # Phase 4 — Pack E (ADR 0010 §Phase 4 — SER coherence + helper-lane).
    src/rules/coherence_hint_redundant_bits.cpp
    src/rules/coherence_hint_encodes_shader_type.cpp
    src/rules/reordercoherent_uav_missing_barrier.cpp
    src/rules/wave_reduction_pixel_without_helper_attribute.cpp
    src/rules/quadany_replaceable_with_derivative_uniform_branch.cpp

    # Phase 7 — Pack DXR (ADR 0017).
    src/rules/oversized_ray_payload.cpp
    src/rules/missing_accept_first_hit.cpp
    src/rules/recursion_depth_not_declared.cpp
    src/rules/live_state_across_traceray.cpp
    src/rules/maybereorderthread_without_payload_shrink.cpp

    # Phase 7 — Pack Mesh (ADR 0017).
    src/rules/meshlet_vertex_count_bad.cpp
    src/rules/output_count_overrun.cpp

    # Phase 7 — Pack Precision/Packing (ADR 0017).
    src/rules/min16float_opportunity.cpp
    src/rules/unpack_then_repack.cpp
    src/rules/manual_f32tof16.cpp

    # Phase 7 — Pack Pressure/Memory (ADR 0017).
    src/rules/vgpr_pressure_warning.cpp
    src/rules/scratch_from_dynamic_indexing.cpp
    src/rules/redundant_texture_sample.cpp
    src/rules/groupshared_when_registers_suffice.cpp
    src/rules/buffer_load_width_vs_cache_line.cpp
)

# Phase 7 — IR-level analysis infrastructure (ADR 0016, sub-phase 7a.1).
#
# Gated behind `HLSL_CLIPPY_ENABLE_IR` (default ON). Sub-phase 7a.1 ships the
# public API surface + a stub engine that always returns
# `clippy::ir-not-implemented`; sub-phase 7a.2 adds the DXC submodule, the
# real `dxil_bridge.cpp` parser, and `cmake/UseDxc.cmake`. The `target_sources`
# call below lands the 7a.1 skeleton; 7a.2 will append `dxil_bridge.cpp` +
# `debug_info.cpp` to the same conditional block.
#
# When the option is OFF, `core/src/ir/` is excluded from the build entirely
# AND `HLSL_CLIPPY_IR_DISABLED` is defined on `hlsl_clippy_core`'s public
# compile flags so the orchestrator can stub `IrEngine::analyze` to return a
# permanent "compiled-out" diagnostic without linking the engine TU.
option(HLSL_CLIPPY_ENABLE_IR
       "Build the Phase 7 IR-level analysis engine (ADR 0016). Default ON."
       ON)

if(HLSL_CLIPPY_ENABLE_IR)
    target_sources(hlsl_clippy_core PRIVATE
        src/ir/engine.cpp
    )
else()
    target_compile_definitions(hlsl_clippy_core PUBLIC HLSL_CLIPPY_IR_DISABLED)
endif()

target_include_directories(hlsl_clippy_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(hlsl_clippy_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(hlsl_clippy_core PUBLIC cxx_std_23)

target_link_libraries(hlsl_clippy_core
    PRIVATE
        hlsl_clippy_warnings
        tree_sitter::tree_sitter
        tree_sitter::hlsl
        tomlplusplus::tomlplusplus
        # Phase 3 — reflection infrastructure (ADR 0012). Slang stays PRIVATE
        # so it never appears in the public include path; only
        # core/src/reflection/slang_bridge.cpp pulls in <slang.h>.
        slang::slang
)

# slang_bridge.cpp needs the Slang public headers; those are propagated to
# `core` compiles via the slang::slang imported target's
# INTERFACE_INCLUDE_DIRECTORIES (set by cmake/UseSlang.cmake). The PRIVATE
# link above keeps Slang headers off the public include path of `core` so
# they remain invisible to CLI / LSP / test consumers.

# Precompiled headers: every rule TU under core/src/rules/ pulls in the
# same set of public headers + STL essentials + tree-sitter's API. With
# 154+ rule TUs, repeating that parse is the dominant cost on cold builds.
# `target_precompile_headers` tells the compiler to compile these once and
# reuse the AST snapshot. Rough win measured by the audit team: 30-50% on
# cold builds, 60%+ on incremental rebuilds where only one rule changes.
#
# PCH is enabled via the standard `BUILD_TESTING`-style opt-out so a
# contributor can disable it for a clean apples-to-apples compile-time
# comparison.
option(HLSL_CLIPPY_USE_PCH "Use precompiled headers for hlsl_clippy_core (faster builds)" ON)
if(HLSL_CLIPPY_USE_PCH)
    target_precompile_headers(hlsl_clippy_core PRIVATE
        # STL essentials every rule TU touches.
        <cstdint>
        <memory>
        <optional>
        <string>
        <string_view>
        <utility>
        <vector>
        # tree-sitter API used by every AST-stage rule.
        <tree_sitter/api.h>
        # Public headers used by every rule.
        "include/hlsl_clippy/rule.hpp"
        "include/hlsl_clippy/diagnostic.hpp"
        "include/hlsl_clippy/source.hpp"
        # Private headers used by most rule TUs.
        "src/parser_internal.hpp"
        "src/rules/rules.hpp"
    )
endif()
