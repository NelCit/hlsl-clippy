cmake_minimum_required(VERSION 3.20)

# Catch2 v3 via FetchContent (per ADR 0005).
include(FetchContent)

# Pin a known-good Catch2 release. v3.5.4 is the first release with stable
# CMake plumbing for FetchContent on MSVC + Ninja.
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)

# Catch2 publishes its own tests; suppress them in our build.
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_DOCS  OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Catch2)

# Path to the fixtures directory, embedded into the test binary so it can find
# the source files regardless of cwd at run-time.
set(HLSL_CLIPPY_FIXTURES_DIR "${CMAKE_SOURCE_DIR}/tests/fixtures")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_config.hpp"
    @ONLY
)

add_executable(hlsl_clippy_unit_tests
    unit/test_pow_const_squared.cpp
    unit/test_suppress.cpp
    unit/test_query.cpp
    unit/test_redundant_saturate.cpp
    unit/test_clamp01_to_saturate.cpp
    unit/test_rewriter.cpp
    unit/test_config.cpp
    unit/test_lerp_extremes.cpp
    unit/test_mul_identity.cpp
    unit/test_sin_cos_pair.cpp
    unit/test_manual_reflect.cpp
    unit/test_manual_refract.cpp
    unit/test_manual_step.cpp
    unit/test_manual_smoothstep.cpp

    # Phase 2 — saturate-redundancy + bit-manipulation rules.
    unit/test_redundant_normalize.cpp
    unit/test_redundant_transpose.cpp
    unit/test_redundant_abs.cpp
    unit/test_countbits_vs_manual_popcount.cpp
    unit/test_firstbit_vs_log2_trick.cpp
    unit/test_manual_mad_decomposition.cpp

    # Phase 2 — pow + vec/length rules.
    unit/test_pow_to_mul.cpp
    unit/test_pow_base_two_to_exp2.cpp
    unit/test_pow_integer_decomposition.cpp
    unit/test_inv_sqrt_to_rsqrt.cpp
    unit/test_manual_distance.cpp
    unit/test_length_comparison.cpp
    unit/test_length_then_divide.cpp
    unit/test_dot_on_axis_aligned_vector.cpp

    # Phase 2 — misc / numerical safety rules.
    unit/test_compare_equal_float.cpp
    unit/test_comparison_with_nan_literal.cpp
    unit/test_redundant_precision_cast.cpp

    # Phase 2 — finalize pack (cross-with-up-vector + ADR 0011 Phase 2 candidates).
    unit/test_cross_with_up_vector.cpp
    unit/test_groupshared_volatile.cpp
    unit/test_lerp_on_bool_cond.cpp
    unit/test_select_vs_lerp_of_constant.cpp
    unit/test_redundant_unorm_snorm_conversion.cpp
    unit/test_wavereadlaneat_constant_zero_to_readfirst.cpp
    unit/test_loop_attribute_conflict.cpp

    # Phase 3 — Pack A (ADR 0011 buffers + groupshared-typed).
    unit/test_byteaddressbuffer_load_misaligned.cpp
    unit/test_byteaddressbuffer_narrow_when_typed_fits.cpp
    unit/test_groupshared_16bit_unpacked.cpp
    unit/test_groupshared_union_aliased.cpp
    unit/test_structured_buffer_stride_not_cache_aligned.cpp

    # Phase 3 — Pack B (ADR 0011 samplers + texture-format).
    unit/test_anisotropy_without_anisotropic_filter.cpp
    unit/test_bgra_rgba_swizzle_mismatch.cpp
    unit/test_comparison_sampler_without_comparison_op.cpp
    unit/test_manual_srgb_conversion.cpp
    unit/test_mip_clamp_zero_on_mipped_texture.cpp
    unit/test_sampler_feedback_without_streaming_flag.cpp
    unit/test_static_sampler_when_dynamic_used.cpp

    # Phase 3 — Pack C (ADR 0011 root-sig + compute + wave + state).
    unit/test_cbuffer_large_fits_rootcbv_not_table.cpp
    unit/test_compute_dispatch_grid_shape_vs_quad.cpp
    unit/test_uav_srv_implicit_transition_assumed.cpp
    unit/test_wavereadlaneat_constant_non_zero_portability.cpp
    unit/test_wavesize_attribute_missing.cpp

    # Phase 3 — Pack D (ADR 0007 §Phase 3 — bindings/texture/workgroup/etc).
    unit/test_all_resources_bound_not_set.cpp
    unit/test_as_payload_over_16k.cpp
    unit/test_bool_straddles_16b.cpp
    unit/test_cbuffer_fits_rootconstants.cpp
    unit/test_cbuffer_padding_hole.cpp
    unit/test_dead_store_sv_target.cpp
    unit/test_descriptor_heap_no_non_uniform_marker.cpp
    unit/test_descriptor_heap_type_confusion.cpp
    unit/test_excess_interpolators.cpp
    unit/test_feedback_write_wrong_stage.cpp
    unit/test_gather_channel_narrowing.cpp
    unit/test_gather_cmp_vs_manual_pcf.cpp
    unit/test_groupshared_too_large.cpp
    unit/test_mesh_numthreads_over_128.cpp
    unit/test_mesh_output_decl_exceeds_256.cpp
    unit/test_min16float_in_cbuffer_roundtrip.cpp
    unit/test_missing_precise_on_pcf.cpp
    unit/test_missing_ray_flag_cull_non_opaque.cpp
    unit/test_nodeid_implicit_mismatch.cpp
    unit/test_nointerpolation_mismatch.cpp
    unit/test_non_uniform_resource_index.cpp
    unit/test_numthreads_not_wave_aligned.cpp
    unit/test_numthreads_too_small.cpp
    unit/test_oversized_cbuffer.cpp
    unit/test_pack_clamp_on_prove_bounded.cpp
    unit/test_rov_without_earlydepthstencil.cpp
    unit/test_rwresource_read_only_usage.cpp
    unit/test_samplecmp_vs_manual_compare.cpp
    unit/test_samplegrad_with_constant_grads.cpp
    unit/test_samplelevel_with_zero_on_mipped_tex.cpp
    unit/test_structured_buffer_stride_mismatch.cpp
    unit/test_sv_depth_vs_conservative_depth.cpp
    unit/test_texture_array_known_slice_uniform.cpp
    unit/test_texture_as_buffer.cpp
    unit/test_texture_lod_bias_without_grad.cpp
    unit/test_unused_cbuffer_field.cpp
    unit/test_vrs_incompatible_output.cpp

    # Phase 3 — Pack E (ADR 0010 §Phase 3 — SM 6.9 SER / CoopVec / Long Vectors / OMM / Mesh Nodes).
    unit/test_coopvec_base_offset_misaligned.cpp
    unit/test_coopvec_fp8_with_non_optimal_layout.cpp
    unit/test_coopvec_non_optimal_matrix_layout.cpp
    unit/test_coopvec_non_uniform_matrix_handle.cpp
    unit/test_coopvec_stride_mismatch.cpp
    unit/test_coopvec_transpose_without_feature_check.cpp
    unit/test_fromrayquery_invoke_without_shader_table.cpp
    unit/test_hitobject_construct_outside_allowed_stages.cpp
    unit/test_hitobject_invoke_after_recursion_cap.cpp
    unit/test_hitobject_passed_to_non_inlined_fn.cpp
    unit/test_hitobject_stored_in_memory.cpp
    unit/test_long_vector_bytebuf_load_misaligned.cpp
    unit/test_long_vector_in_cbuffer_or_signature.cpp
    unit/test_long_vector_non_elementwise_intrinsic.cpp
    unit/test_long_vector_typed_buffer_load.cpp
    unit/test_maybereorderthread_outside_raygen.cpp
    unit/test_mesh_node_missing_output_topology.cpp
    unit/test_mesh_node_not_leaf.cpp
    unit/test_mesh_node_uses_vertex_shader_pipeline.cpp
    unit/test_omm_allocaterayquery2_non_const_flags.cpp
    unit/test_omm_rayquery_force_2state_without_allow_flag.cpp
    unit/test_omm_traceray_force_omm_2state_without_pipeline_flag.cpp
    unit/test_ser_trace_then_invoke_without_reorder.cpp

    # Phase 3 — reflection infrastructure (ADR 0012)
    unit/test_reflection.cpp

    # Phase 3 — shared utilities for reflection-aware rules (ADR 0012 sub-phase 3b).
    unit/test_reflect_util.cpp

    # Phase 5 sub-phase 5a — LSP server scaffolding (ADR 0014).
    unit/test_lsp.cpp

    # Phase 4 sub-phase 4a — control-flow / data-flow infrastructure (ADR 0013).
    unit/test_cfg.cpp

    # Phase 4 sub-phase 4b — shared utilities for control-flow / data-flow rules (ADR 0013).
    unit/test_cfg_util.cpp

    # Phase 4 — Pack A (groupshared microarch).
    unit/test_groupshared_stride_non_32_bank_conflict.cpp
    unit/test_groupshared_dead_store.cpp
    unit/test_groupshared_overwrite_before_barrier.cpp
    unit/test_groupshared_atomic_replaceable_by_wave.cpp
    unit/test_groupshared_first_read_without_barrier.cpp

    # Phase 4 — Pack B (uniformity-aware bindings + mesh + numerical).
    unit/test_divergent_buffer_index_on_uniform_resource.cpp
    unit/test_rwbuffer_store_without_globallycoherent.cpp
    unit/test_primcount_overrun_in_conditional_cf.cpp
    unit/test_dispatchmesh_not_called.cpp
    unit/test_clip_from_non_uniform_cf.cpp
    unit/test_precise_missing_on_iterative_refine.cpp

    # Phase 4 — Pack C (wave + control-flow attributes).
    unit/test_manual_wave_reduction_pattern.cpp
    unit/test_quadany_quadall_opportunity.cpp
    unit/test_wave_prefix_sum_vs_scan_with_atomics.cpp
    unit/test_flatten_on_uniform_branch.cpp
    unit/test_forcecase_missing_on_ps_switch.cpp

    # Phase 4 — Pack D (ADR 0007 §Phase 4 — control-flow / data-flow rule set).
    unit/test_acos_without_saturate.cpp
    unit/test_barrier_in_divergent_cf.cpp
    unit/test_branch_on_uniform_missing_attribute.cpp
    unit/test_cbuffer_divergent_index.cpp
    unit/test_cbuffer_load_in_loop.cpp
    unit/test_derivative_in_divergent_cf.cpp
    unit/test_discard_then_work.cpp
    unit/test_div_without_epsilon.cpp
    unit/test_early_z_disabled_by_conditional_discard.cpp
    unit/test_groupshared_stride_32_bank_conflict.cpp
    unit/test_groupshared_uninitialized_read.cpp
    unit/test_groupshared_write_then_no_barrier_read.cpp
    unit/test_interlocked_bin_without_wave_prereduce.cpp
    unit/test_interlocked_float_bit_cast_trick.cpp
    unit/test_loop_invariant_sample.cpp
    unit/test_redundant_computation_in_branch.cpp
    unit/test_sample_in_loop_implicit_grad.cpp
    unit/test_small_loop_no_unroll.cpp
    unit/test_sqrt_of_potentially_negative.cpp
    unit/test_wave_active_all_equal_precheck.cpp
    unit/test_wave_intrinsic_helper_lane_hazard.cpp
    unit/test_wave_intrinsic_non_uniform.cpp

    # Phase 4 — Pack E (ADR 0010 §Phase 4 — SER coherence + helper-lane).
    unit/test_coherence_hint_redundant_bits.cpp
    unit/test_coherence_hint_encodes_shader_type.cpp
    unit/test_reordercoherent_uav_missing_barrier.cpp
    unit/test_wave_reduction_pixel_without_helper_attribute.cpp
    unit/test_quadany_replaceable_with_derivative_uniform_branch.cpp

    # LSP source files compiled directly into the test target so the unit
    # tests can call into the dispatcher / handlers / framing without
    # depending on the hlsl_clippy_lsp executable.
    "${CMAKE_SOURCE_DIR}/lsp/src/rpc/framing.cpp"
    "${CMAKE_SOURCE_DIR}/lsp/src/rpc/dispatcher.cpp"
    "${CMAKE_SOURCE_DIR}/lsp/src/document/manager.cpp"
    "${CMAKE_SOURCE_DIR}/lsp/src/document/uri.cpp"
    "${CMAKE_SOURCE_DIR}/lsp/src/server/handlers.cpp"
    "${CMAKE_SOURCE_DIR}/lsp/src/server/diagnostic_convert.cpp"
    "${CMAKE_SOURCE_DIR}/lsp/src/server/code_actions.cpp"
    "${CMAKE_SOURCE_DIR}/lsp/src/config_resolver.cpp"
)

target_include_directories(hlsl_clippy_unit_tests PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    # Tests touching the declarative query wrapper need the private core/src
    # headers (parser_internal.hpp, query/query.hpp). Public headers live in
    # core/include and are picked up via the hlsl_clippy_core target's
    # PUBLIC include directory.
    "${CMAKE_SOURCE_DIR}/core/src"
    # LSP private headers — test_lsp.cpp pokes at the dispatcher / handlers
    # / framing without going through the executable target.
    "${CMAKE_SOURCE_DIR}/lsp/src"
)

target_link_libraries(hlsl_clippy_unit_tests
    PRIVATE
        hlsl_clippy_core
        hlsl_clippy_warnings
        Catch2::Catch2WithMain
        tree_sitter::tree_sitter
        tree_sitter::hlsl
        nlohmann_json::nlohmann_json
)

set_target_properties(hlsl_clippy_unit_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Windows: deploy the full Slang runtime DLL constellation (slang.dll +
# 6 transitive dlopen()ed siblings) next to the test exe so Catch2's
# catch_discover_tests step (which runs the exe to enumerate test cases)
# and the eventual ctest runs can resolve dependencies without polluting
# the system PATH. No-op on Linux/macOS where the RPATH handles this.
# See cmake/UseSlang.cmake for details.
hlsl_clippy_deploy_slang_dlls(hlsl_clippy_unit_tests)

# Discover Catch2 test cases as individual ctest entries.
include(Catch)
catch_discover_tests(hlsl_clippy_unit_tests)

# Performance benchmark target (Release / RelWithDebInfo only).
add_subdirectory(bench)
