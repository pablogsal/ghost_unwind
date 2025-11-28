cmake_minimum_required(VERSION 3.14)
project(GhostStack VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Enable testing
enable_testing()

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Detect OS and architecture
if(APPLE)
    set(OS_NAME "darwin")
elseif(UNIX AND NOT APPLE)
    set(OS_NAME "linux")
else()
    message(FATAL_ERROR "Unsupported operating system")
endif()

# Detect architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(ARCH_NAME "aarch64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    set(ARCH_NAME "x86_64")
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

message(STATUS "Building for ${OS_NAME}-${ARCH_NAME}")

# Set the appropriate assembly file based on OS and architecture
set(TRAMPOLINE_SOURCE "${CMAKE_SOURCE_DIR}/src/${ARCH_NAME}_${OS_NAME}_trampoline.s")

# Verify that the assembly file exists
if(NOT EXISTS ${TRAMPOLINE_SOURCE})
    message(FATAL_ERROR "Missing assembly file for ${ARCH_NAME}-${OS_NAME}: ${TRAMPOLINE_SOURCE}")
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fno-omit-frame-pointer -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -fno-omit-frame-pointer -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -fno-omit-frame-pointer")

# Add trampoline assembly to library
find_program(AS_EXECUTABLE as)
if(NOT AS_EXECUTABLE)
    set(AS_EXECUTABLE "as")
endif()

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/trampoline.o
    COMMAND ${AS_EXECUTABLE} ${TRAMPOLINE_SOURCE} -o ${CMAKE_BINARY_DIR}/trampoline.o
    DEPENDS ${TRAMPOLINE_SOURCE}
    COMMENT "Assembling trampoline for ${ARCH_NAME}-${OS_NAME}"
)

# =============================================================================
# Core Library
# =============================================================================

add_library(ghost_stack STATIC
    src/ghost_stack.cpp
    ${CMAKE_BINARY_DIR}/trampoline.o
)

target_include_directories(ghost_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(UNIX AND NOT APPLE)
    target_link_libraries(ghost_stack PUBLIC unwind pthread)
else()
    target_link_libraries(ghost_stack PUBLIC pthread)
endif()

# =============================================================================
# Tests (Google Test)
# =============================================================================

# Main Google Test suite - unified tests
add_executable(ghost_stack_tests
    test/ghost_stack_test.cpp
)
target_link_libraries(ghost_stack_tests PRIVATE
    ghost_stack
    dl
    GTest::gtest_main
)
if(UNIX AND NOT APPLE)
    target_link_libraries(ghost_stack_tests PRIVATE unwind)
endif()

# Register with CTest and enable test discovery
include(GoogleTest)
gtest_discover_tests(ghost_stack_tests)

# Fork test - must be a separate executable (spawns child processes)
add_executable(ghost_stack_test_fork
    test/test_fork.cpp
)
target_link_libraries(ghost_stack_test_fork PRIVATE ghost_stack dl)
add_test(NAME ForkTest COMMAND ghost_stack_test_fork)

# dlclose test: requires a separate shared library embedding ghost_stack
add_library(dlclose_helper SHARED
    test/dlclose_helper.cpp
    src/ghost_stack.cpp
    ${CMAKE_BINARY_DIR}/trampoline.o
)

target_include_directories(dlclose_helper PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(UNIX AND NOT APPLE)
    target_link_libraries(dlclose_helper PUBLIC unwind dl pthread)
else()
    target_link_libraries(dlclose_helper PUBLIC dl pthread)
endif()

set_target_properties(dlclose_helper PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

add_executable(ghost_stack_test_dlclose
    test/test_dlclose.cpp
)
target_link_libraries(ghost_stack_test_dlclose PRIVATE dl)
add_dependencies(ghost_stack_test_dlclose dlclose_helper)
add_test(NAME DlcloseTest COMMAND ghost_stack_test_dlclose WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Benchmark (not a test, just for performance measurement)
add_executable(ghost_stack_bench
    test/bench.cpp
)
target_link_libraries(ghost_stack_bench PRIVATE ghost_stack dl)
if(UNIX AND NOT APPLE)
    target_link_libraries(ghost_stack_bench PRIVATE unwind)
endif()

# =============================================================================
# Examples
# =============================================================================

# LD_PRELOAD library for tracing read() calls
add_library(read_tracer SHARED
    examples/preload.cpp
    src/ghost_stack.cpp
    ${CMAKE_BINARY_DIR}/trampoline.o
)

target_include_directories(read_tracer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(UNIX AND NOT APPLE)
    target_link_libraries(read_tracer PUBLIC unwind dl pthread)
else()
    target_link_libraries(read_tracer PUBLIC dl pthread)
endif()

set_target_properties(read_tracer PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Test program for read tracer
add_executable(test_read
    test/test_read.cpp
)


# =============================================================================
# Install
# =============================================================================

install(TARGETS ghost_stack
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES include/ghost_stack.h
    DESTINATION include
)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "GhostStack Configuration:")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Architecture:   ${ARCH_NAME}")
message(STATUS "  OS:             ${OS_NAME}")
message(STATUS "  C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "")
