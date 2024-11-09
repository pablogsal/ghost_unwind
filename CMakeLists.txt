cmake_minimum_required(VERSION 3.10)
project(GhostStack VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate trampoline assembly
# add_custom_command(
#     OUTPUT ${CMAKE_BINARY_DIR}/trampoline.S
#     COMMAND gcc -O2 -fno-asynchronous-unwind-tables -fno-stack-protector 
#             ${CMAKE_SOURCE_DIR}/src/trampoline_template.cpp
#             -S -o ${CMAKE_BINARY_DIR}/trampoline.S
#     DEPENDS ${CMAKE_SOURCE_DIR}/src/trampoline_template.cpp
# )

# Add trampoline assembly to library
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/trampoline.o
    COMMAND as ${CMAKE_SOURCE_DIR}/src/aarch64_trampoline.s -o ${CMAKE_BINARY_DIR}/trampoline.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/aarch64_trampoline.s
)

# Create ghost stack library
add_library(ghost_stack
    src/ghost_stack.cpp
    ${CMAKE_BINARY_DIR}/trampoline.o
)

target_include_directories(ghost_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# target_link_libraries(ghost_stack PUBLIC
    # unwind
# )

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
add_compile_options(-msign-return-address=none)

# Create preload library
add_library(read_tracer SHARED
    src/preload.cpp
    src/ghost_stack.cpp
    ${CMAKE_BINARY_DIR}/trampoline.o
)

target_include_directories(read_tracer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(read_tracer PUBLIC
    # unwind
    dl
    pthread
)

set_target_properties(read_tracer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Create test program
add_executable(test_read
    test/test_read.cpp
)
