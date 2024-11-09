#!/bin/bash
set -e

# Create build directory
mkdir -p build
cd build

# Configure and build
cmake ..
make

# Run tests
./ghost_stack_test
