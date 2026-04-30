cmake_minimum_required(VERSION 3.20)

# Bring in Slang (idempotent if already included by the parent project).
include("${CMAKE_SOURCE_DIR}/cmake/UseSlang.cmake")

add_executable(slang-smoke src/main.cpp)

target_link_libraries(slang-smoke PRIVATE slang::slang)

# The smoke test needs the Slang public headers.
target_include_directories(slang-smoke PRIVATE
    "${CMAKE_SOURCE_DIR}/external/slang/include"
)

# Enforce the project's C++ standard.
set_target_properties(slang-smoke PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

# Apply our own warning flags, separate from Slang's internal ones.
if(MSVC)
    target_compile_options(slang-smoke PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(slang-smoke PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()
