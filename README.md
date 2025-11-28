<p align="center">
<img src="images/logo.png" width=50%">
</p>

# GhostStack

[![CI](https://github.com/pablogsal/ghost_unwind/actions/workflows/ci.yml/badge.svg)](https://github.com/pablogsal/ghost_unwind/actions/workflows/ci.yml)

Shadow stack implementation for stack unwinding. Drop-in replacement for `unw_backtrace()`.

## Supported Platforms

| Platform | Architecture | Status |
|----------|-------------|--------|
| Linux    | x86_64      | ✓      |
| Linux    | aarch64     | ✓      |
| macOS    | ARM64       | ✓      |

## Building

Requirements:
- CMake 3.10+
- C++17 compiler (GCC or Clang)
- libunwind (Linux only)

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

### Basic API

```c
#include <ghost_stack.h>

// Initialize (optional - auto-initializes on first capture)
ghost_stack_init(NULL);

// Capture stack trace (same signature as unw_backtrace)
void* frames[128];
size_t n = ghost_stack_backtrace(frames, 128);

// Reset when returning to event loop or changing stack significantly
ghost_stack_reset();
```

## Example: LD_PRELOAD Tracer

The included `read_tracer` example intercepts `read()` calls and prints stack traces:

```bash
# Build
cd build && make read_tracer

# Use
LD_PRELOAD=./libread_tracer.so ./your_program
```

## Testing

```bash
cd build
make check
```

## Benchmarking

```bash
cd build
./ghost_stack_bench
```

## How it Works

The first call to `ghost_stack_backtrace()` uses libunwind to walk the stack. For each frame, the return address is read from the stack, saved to a thread-local shadow stack, and replaced with a pointer to an assembly trampoline.

Subsequent calls skip the stack walk entirely and copy instruction pointers directly from the shadow stack.

When a function returns through a patched frame, the trampoline executes instead of returning to the original caller. The trampoline saves the return value registers, calls `ghost_trampoline_handler()` to retrieve the original return address from the shadow stack, restores the registers, and jumps to the original address.

The trampoline includes DWARF unwind information and an LSDA (Language Specific Data Area) so C++ exceptions propagate correctly. The shadow stack also tracks stack pointer values to detect `longjmp` and adjust accordingly.

## License

MIT License

