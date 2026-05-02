#include <memory>
#include <vector>

#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"

#include "rules.hpp"

namespace hlsl_clippy {

std::vector<std::unique_ptr<Rule>> make_default_rules() {
    std::vector<std::unique_ptr<Rule>> rules;
    rules.push_back(rules::make_pow_const_squared());
    rules.push_back(rules::make_redundant_saturate());
    rules.push_back(rules::make_clamp01_to_saturate());
    // Phase 2 — math simplification rules.
    rules.push_back(rules::make_lerp_extremes());
    rules.push_back(rules::make_mul_identity());
    rules.push_back(rules::make_sin_cos_pair());
    rules.push_back(rules::make_manual_reflect());
    rules.push_back(rules::make_manual_refract());
    rules.push_back(rules::make_manual_step());
    rules.push_back(rules::make_manual_smoothstep());
    // Phase 2 — saturate-redundancy + bit-manipulation rules.
    rules.push_back(rules::make_redundant_normalize());
    rules.push_back(rules::make_redundant_transpose());
    rules.push_back(rules::make_redundant_abs());
    rules.push_back(rules::make_countbits_vs_manual_popcount());
    rules.push_back(rules::make_firstbit_vs_log2_trick());
    rules.push_back(rules::make_manual_mad_decomposition());
    // Phase 2 — pow + vec/length rules.
    rules.push_back(rules::make_pow_to_mul());
    rules.push_back(rules::make_pow_base_two_to_exp2());
    rules.push_back(rules::make_pow_integer_decomposition());
    rules.push_back(rules::make_inv_sqrt_to_rsqrt());
    rules.push_back(rules::make_manual_distance());
    rules.push_back(rules::make_length_comparison());
    rules.push_back(rules::make_length_then_divide());
    rules.push_back(rules::make_dot_on_axis_aligned_vector());
    // Phase 2 — misc / numerical safety rules.
    rules.push_back(rules::make_compare_equal_float());
    rules.push_back(rules::make_comparison_with_nan_literal());
    rules.push_back(rules::make_redundant_precision_cast());
    // Phase 2 — finalize pack (cross-with-up-vector + ADR 0011 Phase 2 candidates).
    rules.push_back(rules::make_cross_with_up_vector());
    rules.push_back(rules::make_groupshared_volatile());
    rules.push_back(rules::make_lerp_on_bool_cond());
    rules.push_back(rules::make_select_vs_lerp_of_constant());
    rules.push_back(rules::make_redundant_unorm_snorm_conversion());
    rules.push_back(rules::make_wavereadlaneat_constant_zero_to_readfirst());
    rules.push_back(rules::make_loop_attribute_conflict());
    // Phase 3 — Pack A (ADR 0011 buffers + groupshared-typed).
    rules.push_back(rules::make_byteaddressbuffer_load_misaligned());
    rules.push_back(rules::make_byteaddressbuffer_narrow_when_typed_fits());
    rules.push_back(rules::make_groupshared_16bit_unpacked());
    rules.push_back(rules::make_groupshared_union_aliased());
    rules.push_back(rules::make_structured_buffer_stride_not_cache_aligned());
    // Phase 3 — Pack B (ADR 0011 samplers + texture-format).
    rules.push_back(rules::make_anisotropy_without_anisotropic_filter());
    rules.push_back(rules::make_bgra_rgba_swizzle_mismatch());
    rules.push_back(rules::make_comparison_sampler_without_comparison_op());
    rules.push_back(rules::make_manual_srgb_conversion());
    rules.push_back(rules::make_mip_clamp_zero_on_mipped_texture());
    rules.push_back(rules::make_sampler_feedback_without_streaming_flag());
    rules.push_back(rules::make_static_sampler_when_dynamic_used());
    // Phase 3 — Pack C (ADR 0011 root-sig + compute + wave + state).
    rules.push_back(rules::make_cbuffer_large_fits_rootcbv_not_table());
    rules.push_back(rules::make_compute_dispatch_grid_shape_vs_quad());
    rules.push_back(rules::make_uav_srv_implicit_transition_assumed());
    rules.push_back(rules::make_wavereadlaneat_constant_non_zero_portability());
    rules.push_back(rules::make_wavesize_attribute_missing());
    // Phase 3 — Pack D (ADR 0007 §Phase 3 — bindings/texture/workgroup/etc).
    rules.push_back(rules::make_all_resources_bound_not_set());
    rules.push_back(rules::make_as_payload_over_16k());
    rules.push_back(rules::make_bool_straddles_16b());
    rules.push_back(rules::make_cbuffer_fits_rootconstants());
    rules.push_back(rules::make_cbuffer_padding_hole());
    rules.push_back(rules::make_dead_store_sv_target());
    rules.push_back(rules::make_descriptor_heap_no_non_uniform_marker());
    rules.push_back(rules::make_descriptor_heap_type_confusion());
    rules.push_back(rules::make_excess_interpolators());
    rules.push_back(rules::make_feedback_write_wrong_stage());
    rules.push_back(rules::make_gather_channel_narrowing());
    rules.push_back(rules::make_gather_cmp_vs_manual_pcf());
    rules.push_back(rules::make_groupshared_too_large());
    rules.push_back(rules::make_mesh_numthreads_over_128());
    rules.push_back(rules::make_mesh_output_decl_exceeds_256());
    rules.push_back(rules::make_min16float_in_cbuffer_roundtrip());
    rules.push_back(rules::make_missing_precise_on_pcf());
    rules.push_back(rules::make_missing_ray_flag_cull_non_opaque());
    rules.push_back(rules::make_nodeid_implicit_mismatch());
    rules.push_back(rules::make_nointerpolation_mismatch());
    rules.push_back(rules::make_non_uniform_resource_index());
    rules.push_back(rules::make_numthreads_not_wave_aligned());
    rules.push_back(rules::make_numthreads_too_small());
    rules.push_back(rules::make_oversized_cbuffer());
    rules.push_back(rules::make_pack_clamp_on_prove_bounded());
    rules.push_back(rules::make_rov_without_earlydepthstencil());
    rules.push_back(rules::make_rwresource_read_only_usage());
    rules.push_back(rules::make_samplecmp_vs_manual_compare());
    rules.push_back(rules::make_samplegrad_with_constant_grads());
    rules.push_back(rules::make_samplelevel_with_zero_on_mipped_tex());
    rules.push_back(rules::make_structured_buffer_stride_mismatch());
    rules.push_back(rules::make_sv_depth_vs_conservative_depth());
    rules.push_back(rules::make_texture_array_known_slice_uniform());
    rules.push_back(rules::make_texture_as_buffer());
    rules.push_back(rules::make_texture_lod_bias_without_grad());
    rules.push_back(rules::make_unused_cbuffer_field());
    rules.push_back(rules::make_vrs_incompatible_output());
    // Phase 3 — Pack E (ADR 0010 §Phase 3 — SM 6.9 SER / CoopVec / Long Vectors / OMM / Mesh
    // Nodes).
    rules.push_back(rules::make_coopvec_base_offset_misaligned());
    rules.push_back(rules::make_coopvec_fp8_with_non_optimal_layout());
    rules.push_back(rules::make_coopvec_non_optimal_matrix_layout());
    rules.push_back(rules::make_coopvec_non_uniform_matrix_handle());
    rules.push_back(rules::make_coopvec_stride_mismatch());
    rules.push_back(rules::make_coopvec_transpose_without_feature_check());
    rules.push_back(rules::make_fromrayquery_invoke_without_shader_table());
    rules.push_back(rules::make_hitobject_construct_outside_allowed_stages());
    rules.push_back(rules::make_hitobject_invoke_after_recursion_cap());
    rules.push_back(rules::make_hitobject_passed_to_non_inlined_fn());
    rules.push_back(rules::make_hitobject_stored_in_memory());
    rules.push_back(rules::make_long_vector_bytebuf_load_misaligned());
    rules.push_back(rules::make_long_vector_in_cbuffer_or_signature());
    rules.push_back(rules::make_long_vector_non_elementwise_intrinsic());
    rules.push_back(rules::make_long_vector_typed_buffer_load());
    rules.push_back(rules::make_maybereorderthread_outside_raygen());
    rules.push_back(rules::make_mesh_node_missing_output_topology());
    rules.push_back(rules::make_mesh_node_not_leaf());
    rules.push_back(rules::make_mesh_node_uses_vertex_shader_pipeline());
    rules.push_back(rules::make_omm_allocaterayquery2_non_const_flags());
    rules.push_back(rules::make_omm_rayquery_force_2state_without_allow_flag());
    rules.push_back(rules::make_omm_traceray_force_omm_2state_without_pipeline_flag());
    rules.push_back(rules::make_ser_trace_then_invoke_without_reorder());
    // Phase 4 — Pack A (groupshared microarch).
    rules.push_back(rules::make_groupshared_stride_non_32_bank_conflict());
    rules.push_back(rules::make_groupshared_dead_store());
    rules.push_back(rules::make_groupshared_overwrite_before_barrier());
    rules.push_back(rules::make_groupshared_atomic_replaceable_by_wave());
    rules.push_back(rules::make_groupshared_first_read_without_barrier());
    // Phase 4 — Pack B (uniformity-aware bindings + mesh + numerical).
    rules.push_back(rules::make_divergent_buffer_index_on_uniform_resource());
    rules.push_back(rules::make_rwbuffer_store_without_globallycoherent());
    rules.push_back(rules::make_primcount_overrun_in_conditional_cf());
    rules.push_back(rules::make_dispatchmesh_not_called());
    rules.push_back(rules::make_clip_from_non_uniform_cf());
    rules.push_back(rules::make_precise_missing_on_iterative_refine());
    // Phase 4 — Pack C (wave + control-flow attributes).
    rules.push_back(rules::make_manual_wave_reduction_pattern());
    rules.push_back(rules::make_quadany_quadall_opportunity());
    rules.push_back(rules::make_wave_prefix_sum_vs_scan_with_atomics());
    rules.push_back(rules::make_flatten_on_uniform_branch());
    rules.push_back(rules::make_forcecase_missing_on_ps_switch());
    // Phase 4 — Pack D (ADR 0007 §Phase 4 — control-flow / data-flow rule set).
    rules.push_back(rules::make_acos_without_saturate());
    rules.push_back(rules::make_barrier_in_divergent_cf());
    rules.push_back(rules::make_branch_on_uniform_missing_attribute());
    rules.push_back(rules::make_cbuffer_divergent_index());
    rules.push_back(rules::make_cbuffer_load_in_loop());
    rules.push_back(rules::make_derivative_in_divergent_cf());
    rules.push_back(rules::make_discard_then_work());
    rules.push_back(rules::make_div_without_epsilon());
    rules.push_back(rules::make_early_z_disabled_by_conditional_discard());
    rules.push_back(rules::make_groupshared_stride_32_bank_conflict());
    rules.push_back(rules::make_groupshared_uninitialized_read());
    rules.push_back(rules::make_groupshared_write_then_no_barrier_read());
    rules.push_back(rules::make_interlocked_bin_without_wave_prereduce());
    rules.push_back(rules::make_interlocked_float_bit_cast_trick());
    rules.push_back(rules::make_loop_invariant_sample());
    rules.push_back(rules::make_redundant_computation_in_branch());
    rules.push_back(rules::make_sample_in_loop_implicit_grad());
    rules.push_back(rules::make_small_loop_no_unroll());
    rules.push_back(rules::make_sqrt_of_potentially_negative());
    rules.push_back(rules::make_wave_active_all_equal_precheck());
    rules.push_back(rules::make_wave_intrinsic_helper_lane_hazard());
    rules.push_back(rules::make_wave_intrinsic_non_uniform());
    // Phase 4 — Pack E (ADR 0010 §Phase 4 — SER coherence + helper-lane).
    rules.push_back(rules::make_coherence_hint_redundant_bits());
    rules.push_back(rules::make_coherence_hint_encodes_shader_type());
    rules.push_back(rules::make_reordercoherent_uav_missing_barrier());
    rules.push_back(rules::make_wave_reduction_pixel_without_helper_attribute());
    rules.push_back(rules::make_quadany_replaceable_with_derivative_uniform_branch());
    // Phase 7 — Pack DXR / Mesh / Precision / Pressure (ADR 0017)
    rules.push_back(rules::make_oversized_ray_payload());
    rules.push_back(rules::make_missing_accept_first_hit());
    rules.push_back(rules::make_recursion_depth_not_declared());
    rules.push_back(rules::make_live_state_across_traceray());
    rules.push_back(rules::make_maybereorderthread_without_payload_shrink());
    rules.push_back(rules::make_meshlet_vertex_count_bad());
    rules.push_back(rules::make_output_count_overrun());
    rules.push_back(rules::make_min16float_opportunity());
    rules.push_back(rules::make_unpack_then_repack());
    rules.push_back(rules::make_manual_f32tof16());
    rules.push_back(rules::make_vgpr_pressure_warning());
    rules.push_back(rules::make_scratch_from_dynamic_indexing());
    rules.push_back(rules::make_redundant_texture_sample());
    rules.push_back(rules::make_groupshared_when_registers_suffice());
    rules.push_back(rules::make_buffer_load_width_vs_cache_line());
    // Phase 8 — Pack v0.8 SM 6.10 + stub burndown (ADR 0018).
    rules.push_back(rules::make_linalg_matrix_non_optimal_layout());
    rules.push_back(rules::make_linalg_matrix_element_type_mismatch());
    rules.push_back(rules::make_getgroupwaveindex_without_wavesize_attribute());
    rules.push_back(rules::make_groupshared_over_32k_without_attribute());
    rules.push_back(rules::make_triangle_object_positions_without_allow_data_access_flag());
    rules.push_back(rules::make_dispatchmesh_grid_too_small_for_wave());
    rules.push_back(rules::make_dot4add_opportunity());
    // Phase 8 — Pack v0.9 VRS + DXR + Nsight-gap (ADR 0018).
    rules.push_back(rules::make_vrs_rate_conflict_with_target());
    rules.push_back(rules::make_vrs_without_perprimitive_or_screenspace_source());
    rules.push_back(rules::make_ray_flag_force_opaque_with_anyhit());
    rules.push_back(rules::make_ser_coherence_hint_bits_overflow());
    rules.push_back(rules::make_sample_use_no_interleave());
    // Phase 8 — Pack v0.10 IHV-experimental (ADR 0018).
    rules.push_back(rules::make_wave64_on_rdna4_compute_misses_dynamic_vgpr());
    rules.push_back(rules::make_coopvec_fp4_fp6_blackwell_layout());
    rules.push_back(rules::make_wavesize_32_on_xe2_misses_simd16());
    rules.push_back(rules::make_cluster_id_without_cluster_geometry_feature_check());
    // Phase 8 — Pack DEFERRED candidates (ADR 0018).
    rules.push_back(rules::make_oriented_bbox_not_set_on_rdna4());
    rules.push_back(rules::make_numwaves_anchored_cap());
    rules.push_back(rules::make_reference_data_type_not_supported_pre_sm610());
    rules.push_back(rules::make_rga_pressure_bridge_stub());
    // Phase 8.C — Slang-specific rules (ADR 0021 sub-phase C).
    rules.push_back(rules::make_slang_generic_without_constraint());
    rules.push_back(rules::make_slang_interface_conformance_missing_method());
    rules.push_back(rules::make_slang_module_import_without_use());
    rules.push_back(rules::make_slang_associatedtype_shadowing_builtin());
    return rules;
}

}  // namespace hlsl_clippy
