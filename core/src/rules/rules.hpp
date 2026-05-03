// Internal-only header listing the factory functions for the rules shipped in
// the default pack. Each rule has its own translation unit; the registry pulls
// them all in.

#pragma once

#include <memory>

#include "shader_clippy/rule.hpp"

namespace shader_clippy::rules {

[[nodiscard]] std::unique_ptr<Rule> make_pow_const_squared();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_saturate();
[[nodiscard]] std::unique_ptr<Rule> make_clamp01_to_saturate();

// Phase 2 — math simplification rules.
[[nodiscard]] std::unique_ptr<Rule> make_lerp_extremes();
[[nodiscard]] std::unique_ptr<Rule> make_mul_identity();
[[nodiscard]] std::unique_ptr<Rule> make_sin_cos_pair();
[[nodiscard]] std::unique_ptr<Rule> make_manual_reflect();
[[nodiscard]] std::unique_ptr<Rule> make_manual_refract();
[[nodiscard]] std::unique_ptr<Rule> make_manual_step();
[[nodiscard]] std::unique_ptr<Rule> make_manual_smoothstep();

// Phase 2 — saturate-redundancy + bit-manipulation rules.
[[nodiscard]] std::unique_ptr<Rule> make_redundant_normalize();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_transpose();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_abs();
[[nodiscard]] std::unique_ptr<Rule> make_countbits_vs_manual_popcount();
[[nodiscard]] std::unique_ptr<Rule> make_firstbit_vs_log2_trick();
[[nodiscard]] std::unique_ptr<Rule> make_manual_mad_decomposition();

// Phase 2 — pow + vec/length rules.
[[nodiscard]] std::unique_ptr<Rule> make_pow_to_mul();
[[nodiscard]] std::unique_ptr<Rule> make_pow_base_two_to_exp2();
[[nodiscard]] std::unique_ptr<Rule> make_pow_integer_decomposition();
[[nodiscard]] std::unique_ptr<Rule> make_inv_sqrt_to_rsqrt();
[[nodiscard]] std::unique_ptr<Rule> make_manual_distance();
[[nodiscard]] std::unique_ptr<Rule> make_length_comparison();
[[nodiscard]] std::unique_ptr<Rule> make_length_then_divide();
[[nodiscard]] std::unique_ptr<Rule> make_dot_on_axis_aligned_vector();

// Phase 2 — misc / numerical safety rules.
[[nodiscard]] std::unique_ptr<Rule> make_compare_equal_float();
[[nodiscard]] std::unique_ptr<Rule> make_comparison_with_nan_literal();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_precision_cast();

// Phase 2 — finalize pack (cross-with-up-vector + ADR 0011 Phase 2 candidates).
[[nodiscard]] std::unique_ptr<Rule> make_cross_with_up_vector();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_volatile();
[[nodiscard]] std::unique_ptr<Rule> make_lerp_on_bool_cond();
[[nodiscard]] std::unique_ptr<Rule> make_select_vs_lerp_of_constant();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_unorm_snorm_conversion();
[[nodiscard]] std::unique_ptr<Rule> make_wavereadlaneat_constant_zero_to_readfirst();
[[nodiscard]] std::unique_ptr<Rule> make_loop_attribute_conflict();

// Phase 3 — Pack A (ADR 0011 buffers + groupshared-typed).
[[nodiscard]] std::unique_ptr<Rule> make_byteaddressbuffer_load_misaligned();
[[nodiscard]] std::unique_ptr<Rule> make_byteaddressbuffer_narrow_when_typed_fits();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_16bit_unpacked();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_union_aliased();
[[nodiscard]] std::unique_ptr<Rule> make_structured_buffer_stride_not_cache_aligned();

// Phase 3 — Pack B (ADR 0011 samplers + texture-format).
[[nodiscard]] std::unique_ptr<Rule> make_anisotropy_without_anisotropic_filter();
[[nodiscard]] std::unique_ptr<Rule> make_bgra_rgba_swizzle_mismatch();
[[nodiscard]] std::unique_ptr<Rule> make_comparison_sampler_without_comparison_op();
[[nodiscard]] std::unique_ptr<Rule> make_manual_srgb_conversion();
[[nodiscard]] std::unique_ptr<Rule> make_mip_clamp_zero_on_mipped_texture();
[[nodiscard]] std::unique_ptr<Rule> make_sampler_feedback_without_streaming_flag();
[[nodiscard]] std::unique_ptr<Rule> make_static_sampler_when_dynamic_used();

// Phase 3 — Pack C (ADR 0011 root-sig + compute + wave + state).
[[nodiscard]] std::unique_ptr<Rule> make_cbuffer_large_fits_rootcbv_not_table();
[[nodiscard]] std::unique_ptr<Rule> make_compute_dispatch_grid_shape_vs_quad();
[[nodiscard]] std::unique_ptr<Rule> make_uav_srv_implicit_transition_assumed();
[[nodiscard]] std::unique_ptr<Rule> make_wavereadlaneat_constant_non_zero_portability();
[[nodiscard]] std::unique_ptr<Rule> make_wavesize_attribute_missing();

// Phase 3 — Pack D (ADR 0007 §Phase 3 — bindings/texture/workgroup/etc).
[[nodiscard]] std::unique_ptr<Rule> make_all_resources_bound_not_set();
[[nodiscard]] std::unique_ptr<Rule> make_as_payload_over_16k();
[[nodiscard]] std::unique_ptr<Rule> make_bool_straddles_16b();
[[nodiscard]] std::unique_ptr<Rule> make_cbuffer_fits_rootconstants();
[[nodiscard]] std::unique_ptr<Rule> make_cbuffer_padding_hole();
[[nodiscard]] std::unique_ptr<Rule> make_dead_store_sv_target();
[[nodiscard]] std::unique_ptr<Rule> make_descriptor_heap_no_non_uniform_marker();
[[nodiscard]] std::unique_ptr<Rule> make_descriptor_heap_type_confusion();
[[nodiscard]] std::unique_ptr<Rule> make_excess_interpolators();
[[nodiscard]] std::unique_ptr<Rule> make_feedback_write_wrong_stage();
[[nodiscard]] std::unique_ptr<Rule> make_gather_channel_narrowing();
[[nodiscard]] std::unique_ptr<Rule> make_gather_cmp_vs_manual_pcf();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_too_large();
[[nodiscard]] std::unique_ptr<Rule> make_mesh_numthreads_over_128();
[[nodiscard]] std::unique_ptr<Rule> make_mesh_output_decl_exceeds_256();
[[nodiscard]] std::unique_ptr<Rule> make_min16float_in_cbuffer_roundtrip();
[[nodiscard]] std::unique_ptr<Rule> make_missing_precise_on_pcf();
[[nodiscard]] std::unique_ptr<Rule> make_missing_ray_flag_cull_non_opaque();
[[nodiscard]] std::unique_ptr<Rule> make_nodeid_implicit_mismatch();
[[nodiscard]] std::unique_ptr<Rule> make_nointerpolation_mismatch();
[[nodiscard]] std::unique_ptr<Rule> make_non_uniform_resource_index();
[[nodiscard]] std::unique_ptr<Rule> make_numthreads_not_wave_aligned();
[[nodiscard]] std::unique_ptr<Rule> make_numthreads_too_small();
[[nodiscard]] std::unique_ptr<Rule> make_oversized_cbuffer();
[[nodiscard]] std::unique_ptr<Rule> make_pack_clamp_on_prove_bounded();
[[nodiscard]] std::unique_ptr<Rule> make_rov_without_earlydepthstencil();
[[nodiscard]] std::unique_ptr<Rule> make_rwresource_read_only_usage();
[[nodiscard]] std::unique_ptr<Rule> make_samplecmp_vs_manual_compare();
[[nodiscard]] std::unique_ptr<Rule> make_samplegrad_with_constant_grads();
[[nodiscard]] std::unique_ptr<Rule> make_samplelevel_with_zero_on_mipped_tex();
[[nodiscard]] std::unique_ptr<Rule> make_structured_buffer_stride_mismatch();
[[nodiscard]] std::unique_ptr<Rule> make_sv_depth_vs_conservative_depth();
[[nodiscard]] std::unique_ptr<Rule> make_texture_array_known_slice_uniform();
[[nodiscard]] std::unique_ptr<Rule> make_texture_as_buffer();
[[nodiscard]] std::unique_ptr<Rule> make_texture_lod_bias_without_grad();
[[nodiscard]] std::unique_ptr<Rule> make_unused_cbuffer_field();
[[nodiscard]] std::unique_ptr<Rule> make_vrs_incompatible_output();

// Phase 3 — Pack E (ADR 0010 §Phase 3 — SM 6.9 SER / CoopVec / Long Vectors / OMM / Mesh Nodes).
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_base_offset_misaligned();
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_fp8_with_non_optimal_layout();
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_non_optimal_matrix_layout();
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_non_uniform_matrix_handle();
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_stride_mismatch();
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_transpose_without_feature_check();
[[nodiscard]] std::unique_ptr<Rule> make_fromrayquery_invoke_without_shader_table();
[[nodiscard]] std::unique_ptr<Rule> make_hitobject_construct_outside_allowed_stages();
[[nodiscard]] std::unique_ptr<Rule> make_hitobject_invoke_after_recursion_cap();
[[nodiscard]] std::unique_ptr<Rule> make_hitobject_passed_to_non_inlined_fn();
[[nodiscard]] std::unique_ptr<Rule> make_hitobject_stored_in_memory();
[[nodiscard]] std::unique_ptr<Rule> make_long_vector_bytebuf_load_misaligned();
[[nodiscard]] std::unique_ptr<Rule> make_long_vector_in_cbuffer_or_signature();
[[nodiscard]] std::unique_ptr<Rule> make_long_vector_non_elementwise_intrinsic();
[[nodiscard]] std::unique_ptr<Rule> make_long_vector_typed_buffer_load();
[[nodiscard]] std::unique_ptr<Rule> make_maybereorderthread_outside_raygen();
[[nodiscard]] std::unique_ptr<Rule> make_mesh_node_missing_output_topology();
[[nodiscard]] std::unique_ptr<Rule> make_mesh_node_not_leaf();
[[nodiscard]] std::unique_ptr<Rule> make_mesh_node_uses_vertex_shader_pipeline();
[[nodiscard]] std::unique_ptr<Rule> make_omm_allocaterayquery2_non_const_flags();
[[nodiscard]] std::unique_ptr<Rule> make_omm_rayquery_force_2state_without_allow_flag();
[[nodiscard]] std::unique_ptr<Rule> make_omm_traceray_force_omm_2state_without_pipeline_flag();
[[nodiscard]] std::unique_ptr<Rule> make_ser_trace_then_invoke_without_reorder();

// Phase 4 — Pack A (groupshared microarch).
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_stride_non_32_bank_conflict();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_dead_store();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_overwrite_before_barrier();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_atomic_replaceable_by_wave();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_first_read_without_barrier();

// Phase 4 — Pack B (uniformity-aware bindings + mesh + numerical).
[[nodiscard]] std::unique_ptr<Rule> make_divergent_buffer_index_on_uniform_resource();
[[nodiscard]] std::unique_ptr<Rule> make_rwbuffer_store_without_globallycoherent();
[[nodiscard]] std::unique_ptr<Rule> make_primcount_overrun_in_conditional_cf();
[[nodiscard]] std::unique_ptr<Rule> make_dispatchmesh_not_called();
[[nodiscard]] std::unique_ptr<Rule> make_clip_from_non_uniform_cf();
[[nodiscard]] std::unique_ptr<Rule> make_precise_missing_on_iterative_refine();

// Phase 4 — Pack C (wave + control-flow attributes).
[[nodiscard]] std::unique_ptr<Rule> make_manual_wave_reduction_pattern();
[[nodiscard]] std::unique_ptr<Rule> make_quadany_quadall_opportunity();
[[nodiscard]] std::unique_ptr<Rule> make_wave_prefix_sum_vs_scan_with_atomics();
[[nodiscard]] std::unique_ptr<Rule> make_flatten_on_uniform_branch();
[[nodiscard]] std::unique_ptr<Rule> make_forcecase_missing_on_ps_switch();

// Phase 4 — Pack D (ADR 0007 §Phase 4 — control-flow / data-flow rule set).
[[nodiscard]] std::unique_ptr<Rule> make_acos_without_saturate();
[[nodiscard]] std::unique_ptr<Rule> make_barrier_in_divergent_cf();
[[nodiscard]] std::unique_ptr<Rule> make_branch_on_uniform_missing_attribute();
[[nodiscard]] std::unique_ptr<Rule> make_cbuffer_divergent_index();
[[nodiscard]] std::unique_ptr<Rule> make_cbuffer_load_in_loop();
[[nodiscard]] std::unique_ptr<Rule> make_derivative_in_divergent_cf();
[[nodiscard]] std::unique_ptr<Rule> make_discard_then_work();
[[nodiscard]] std::unique_ptr<Rule> make_div_without_epsilon();
[[nodiscard]] std::unique_ptr<Rule> make_early_z_disabled_by_conditional_discard();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_stride_32_bank_conflict();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_uninitialized_read();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_write_then_no_barrier_read();
[[nodiscard]] std::unique_ptr<Rule> make_interlocked_bin_without_wave_prereduce();
[[nodiscard]] std::unique_ptr<Rule> make_interlocked_float_bit_cast_trick();
[[nodiscard]] std::unique_ptr<Rule> make_loop_invariant_sample();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_computation_in_branch();
[[nodiscard]] std::unique_ptr<Rule> make_sample_in_loop_implicit_grad();
[[nodiscard]] std::unique_ptr<Rule> make_small_loop_no_unroll();
[[nodiscard]] std::unique_ptr<Rule> make_sqrt_of_potentially_negative();
[[nodiscard]] std::unique_ptr<Rule> make_wave_active_all_equal_precheck();
[[nodiscard]] std::unique_ptr<Rule> make_wave_intrinsic_helper_lane_hazard();
[[nodiscard]] std::unique_ptr<Rule> make_wave_intrinsic_non_uniform();

// Phase 4 — Pack E (ADR 0010 §Phase 4 — SER coherence + helper-lane).
[[nodiscard]] std::unique_ptr<Rule> make_coherence_hint_redundant_bits();
[[nodiscard]] std::unique_ptr<Rule> make_coherence_hint_encodes_shader_type();
[[nodiscard]] std::unique_ptr<Rule> make_reordercoherent_uav_missing_barrier();
[[nodiscard]] std::unique_ptr<Rule> make_wave_reduction_pixel_without_helper_attribute();
[[nodiscard]] std::unique_ptr<Rule> make_quadany_replaceable_with_derivative_uniform_branch();

// Phase 7 — Pack DXR / Mesh / Precision / Pressure (ADR 0017).
[[nodiscard]] std::unique_ptr<Rule> make_oversized_ray_payload();
[[nodiscard]] std::unique_ptr<Rule> make_missing_accept_first_hit();
[[nodiscard]] std::unique_ptr<Rule> make_recursion_depth_not_declared();
[[nodiscard]] std::unique_ptr<Rule> make_live_state_across_traceray();
[[nodiscard]] std::unique_ptr<Rule> make_maybereorderthread_without_payload_shrink();
[[nodiscard]] std::unique_ptr<Rule> make_meshlet_vertex_count_bad();
[[nodiscard]] std::unique_ptr<Rule> make_output_count_overrun();
[[nodiscard]] std::unique_ptr<Rule> make_min16float_opportunity();
[[nodiscard]] std::unique_ptr<Rule> make_unpack_then_repack();
[[nodiscard]] std::unique_ptr<Rule> make_manual_f32tof16();
[[nodiscard]] std::unique_ptr<Rule> make_vgpr_pressure_warning();
[[nodiscard]] std::unique_ptr<Rule> make_scratch_from_dynamic_indexing();
[[nodiscard]] std::unique_ptr<Rule> make_redundant_texture_sample();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_when_registers_suffice();
[[nodiscard]] std::unique_ptr<Rule> make_buffer_load_width_vs_cache_line();

// Phase 8 — Pack v0.8 SM 6.10 + stub burndown (ADR 0018).
[[nodiscard]] std::unique_ptr<Rule> make_linalg_matrix_non_optimal_layout();
[[nodiscard]] std::unique_ptr<Rule> make_linalg_matrix_element_type_mismatch();
[[nodiscard]] std::unique_ptr<Rule> make_getgroupwaveindex_without_wavesize_attribute();
[[nodiscard]] std::unique_ptr<Rule> make_groupshared_over_32k_without_attribute();
[[nodiscard]] std::unique_ptr<Rule> make_triangle_object_positions_without_allow_data_access_flag();
[[nodiscard]] std::unique_ptr<Rule> make_dispatchmesh_grid_too_small_for_wave();
[[nodiscard]] std::unique_ptr<Rule> make_dot4add_opportunity();

// Phase 8 — Pack v0.9 VRS + DXR + Nsight-gap (ADR 0018).
[[nodiscard]] std::unique_ptr<Rule> make_vrs_rate_conflict_with_target();
[[nodiscard]] std::unique_ptr<Rule> make_vrs_without_perprimitive_or_screenspace_source();
[[nodiscard]] std::unique_ptr<Rule> make_ray_flag_force_opaque_with_anyhit();
[[nodiscard]] std::unique_ptr<Rule> make_ser_coherence_hint_bits_overflow();
[[nodiscard]] std::unique_ptr<Rule> make_sample_use_no_interleave();

// Phase 8 — Pack v0.10 IHV-experimental (ADR 0018).
[[nodiscard]] std::unique_ptr<Rule> make_wave64_on_rdna4_compute_misses_dynamic_vgpr();
[[nodiscard]] std::unique_ptr<Rule> make_coopvec_fp4_fp6_blackwell_layout();
[[nodiscard]] std::unique_ptr<Rule> make_wavesize_32_on_xe2_misses_simd16();
[[nodiscard]] std::unique_ptr<Rule> make_cluster_id_without_cluster_geometry_feature_check();

// Phase 8 — Pack DEFERRED candidates (ADR 0018).
[[nodiscard]] std::unique_ptr<Rule> make_oriented_bbox_not_set_on_rdna4();
[[nodiscard]] std::unique_ptr<Rule> make_numwaves_anchored_cap();
[[nodiscard]] std::unique_ptr<Rule> make_reference_data_type_not_supported_pre_sm610();
[[nodiscard]] std::unique_ptr<Rule> make_rga_pressure_bridge_stub();

// Phase 8.C — Slang-specific rules (ADR 0021 sub-phase C).
[[nodiscard]] std::unique_ptr<Rule> make_slang_generic_without_constraint();
[[nodiscard]] std::unique_ptr<Rule> make_slang_interface_conformance_missing_method();
[[nodiscard]] std::unique_ptr<Rule> make_slang_module_import_without_use();
[[nodiscard]] std::unique_ptr<Rule> make_slang_associatedtype_shadowing_builtin();

}  // namespace shader_clippy::rules
