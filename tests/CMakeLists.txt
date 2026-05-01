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

    # Phase 3 — reflection infrastructure (ADR 0012)
    unit/test_reflection.cpp
)

target_include_directories(hlsl_clippy_unit_tests PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    # Tests touching the declarative query wrapper need the private core/src
    # headers (parser_internal.hpp, query/query.hpp). Public headers live in
    # core/include and are picked up via the hlsl_clippy_core target's
    # PUBLIC include directory.
    "${CMAKE_SOURCE_DIR}/core/src"
)

target_link_libraries(hlsl_clippy_unit_tests
    PRIVATE
        hlsl_clippy_core
        hlsl_clippy_warnings
        Catch2::Catch2WithMain
        tree_sitter::tree_sitter
        tree_sitter::hlsl
)

set_target_properties(hlsl_clippy_unit_tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Discover Catch2 test cases as individual ctest entries.
include(Catch)
catch_discover_tests(hlsl_clippy_unit_tests)
