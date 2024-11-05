cmake_minimum_required(VERSION 3.10)
project(ShadowStack VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate trampoline assembly
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/trampoline.S
    COMMAND gcc -O2 -fno-asynchronous-unwind-tables -fno-stack-protector 
            ${CMAKE_SOURCE_DIR}/src/trampoline_template.c
            -S -o ${CMAKE_BINARY_DIR}/trampoline.S
    DEPENDS ${CMAKE_SOURCE_DIR}/src/trampoline_template.c
)

# Add trampoline assembly to library
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/trampoline.o
    COMMAND as ${CMAKE_BINARY_DIR}/trampoline.S -o ${CMAKE_BINARY_DIR}/trampoline.o
    DEPENDS ${CMAKE_BINARY_DIR}/trampoline.S
)

# Create shadow stack library
add_library(shadow_stack
    src/shadow_stack.cpp
    ${CMAKE_BINARY_DIR}/trampoline.o
)

target_include_directories(shadow_stack PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(shadow_stack PUBLIC
    unwind
)

# Create test executable
add_executable(shadow_stack_test
    test/main.cpp
)

target_link_libraries(shadow_stack_test PRIVATE
    shadow_stack
)

set(CMAKE_BUILD_TYPE Debug)
add_compile_options(-g -O0 -fno-omit-frame-pointer)
add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_options(-DDEBUG)

# Create preload library
add_library(read_tracer SHARED
    src/preload.cpp
    src/shadow_stack.cpp
    ${CMAKE_BINARY_DIR}/trampoline.o
)

target_include_directories(read_tracer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(read_tracer PUBLIC
    unwind
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
