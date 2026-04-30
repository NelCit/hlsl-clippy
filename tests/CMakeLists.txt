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
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Discover Catch2 test cases as individual ctest entries.
include(Catch)
catch_discover_tests(hlsl_clippy_unit_tests)
