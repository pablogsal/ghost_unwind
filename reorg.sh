#!/bin/bash

# Create project structure
mkdir -p src include test cmake

# Move files to appropriate directories
mv shadow_stack.cpp src/
mv shadow_stack.hpp include/
mv main.cpp test/
mv trampoline_template.c src/
mv build.sh scripts/

# Create CMake files
cat > CMakeLists.txt << 'EOF'
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
EOF

# Create README
cat > README.md << 'EOF'
# Shadow Stack Implementation

A fast stack unwinding implementation using shadow stacks, inspired by the Bytehound profiler technique.

## Overview

This project implements a shadow stack mechanism for fast stack unwinding. The technique involves:
1. Patching return addresses with a trampoline
2. Maintaining a shadow stack of original return addresses
3. Using the shadow stack for fast unwinding

## Building

Requirements:
- CMake 3.10 or higher
- GCC
- libunwind

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

```cpp
#include <shadow_stack.hpp>

void print_stack_trace() {
    auto trace = ShadowStack::get().unwind();
    // Process trace...
}
```

## How it Works

The shadow stack implementation:
1. Uses libunwind to walk the stack initially
2. Patches return addresses with a trampoline function
3. Stores original return addresses in a shadow stack
4. Subsequent unwinds use the shadow stack entries

## License

MIT License

EOF

# Create build script
mkdir -p scripts
cat > scripts/build.sh << 'EOF'
#!/bin/bash
set -e

# Create build directory
mkdir -p build
cd build

# Configure and build
cmake ..
make

# Run tests
./shadow_stack_test
EOF

chmod +x scripts/build.sh

# Clean up object files and temporary files
rm -f *.o *.S.bak *.tmp.S

# Create .gitignore
cat > .gitignore << 'EOF'
build/
*.o
*.a
*.so
.vscode/
.idea/
CMakeFiles/
cmake-build-*/
*.S.bak
*.tmp.S
EOF

# Initialize git repository
git init
git add .
git commit -m "Initial commit"

echo "Project structure created. You can now build the project with:"
echo "mkdir build && cd build && cmake .. && make"
