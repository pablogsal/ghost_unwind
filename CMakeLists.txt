cmake_minimum_required(VERSION 3.10)
project(GhostStack VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Set the appropriate assembly file based on OS and architecture
set(TRAMPOLINE_SOURCE "${CMAKE_SOURCE_DIR}/src/${ARCH_NAME}_${OS_NAME}_trampoline.s")

# Verify that the assembly file exists
if(NOT EXISTS ${TRAMPOLINE_SOURCE})
    message(FATAL_ERROR "Missing assembly file for ${ARCH_NAME}-${OS_NAME}: ${TRAMPOLINE_SOURCE}")
endif()

# Add trampoline assembly to library
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/trampoline.o
    COMMAND as ${TRAMPOLINE_SOURCE} -o ${CMAKE_BINARY_DIR}/trampoline.o
    DEPENDS ${TRAMPOLINE_SOURCE}
)

# Create ghost stack library
add_library(ghost_stack
    src/ghost_stack.cpp
    ${CMAKE_BINARY_DIR}/trampoline.o
)

target_include_directories(ghost_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Only link libunwind on Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(ghost_stack PUBLIC unwind)
endif()

# Create test executable
add_executable(ghost_stack_test
    test/main.cpp
)

target_link_libraries(ghost_stack_test PRIVATE
    ghost_stack
)

set(CMAKE_BUILD_TYPE Debug)
add_compile_options(-g -O0 -fno-omit-frame-pointer)
add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_options(-DDEBUG)

# Create preload library
add_library(read_tracer SHARED
    src/preload.cpp
    src/ghost_stack.cpp
    ${CMAKE_BINARY_DIR}/trampoline.o
)

target_include_directories(read_tracer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Conditionally link libraries based on OS
if(UNIX AND NOT APPLE)
    target_link_libraries(read_tracer PUBLIC
        unwind
        dl
        pthread
    )
else()
    target_link_libraries(read_tracer PUBLIC
        dl
        pthread
    )
endif()

set_target_properties(read_tracer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Create test program
add_executable(test_read
    test/test_read.cpp
)
