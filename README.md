# Shadow Stack Implementation

A fast stack unwinding implementation using shadow stacks

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

