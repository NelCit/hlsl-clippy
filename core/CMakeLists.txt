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
)

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
