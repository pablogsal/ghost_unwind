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

GhostStack achieves O(1) stack capture by maintaining a **shadow stack**—a thread-local data structure that mirrors the call stack's return addresses. After an initial full stack walk, subsequent captures simply copy from this shadow stack, avoiding expensive DWARF parsing and register state reconstruction.

### Architecture Overview

**First Capture (Slow Path):** libunwind walks the stack, collecting IPs and return address locations. For each frame, we save `{IP, original_ret_addr, stack_location, SP}`, then patch each return address on the stack to point to the trampoline. Finally, return the IPs to the caller.

**Subsequent Captures (Fast Path):** Check the `trampolines_installed_` flag, copy IPs from shadow stack entries to the output buffer, and return immediately—no stack walk, no DWARF parsing.

### Shadow Stack Data Structure

Each entry in the shadow stack stores four values: the instruction pointer (what to report to the caller), the original return address (what we replaced), the memory address on the stack where that return address lives, and the stack pointer at capture time (used for longjmp detection).

Entries are stored in **reverse order**: index 0 is the oldest frame (bottom of call stack), and the tail index points past the newest frame. This ordering allows efficient pop operations when functions return through trampolines—simply decrement the tail pointer.

### Return Address Patching

During the initial capture, GhostStack uses `unw_get_save_loc()` (Linux) or frame pointer arithmetic (macOS) to locate where each frame's return address is stored on the stack:

```
Before patching:                    After patching:
┌──────────────┐                    ┌──────────────┐
│   ...        │                    │   ...        │
├──────────────┤                    ├──────────────┤
│ ret addr → A │  ──────────────►   │ ret addr → T │  (T = trampoline)
├──────────────┤                    ├──────────────┤
│ saved rbp    │                    │ saved rbp    │
├──────────────┤                    ├──────────────┤
│ local vars   │                    │ local vars   │
└──────────────┘                    └──────────────┘
```

When the function executes `ret`, instead of returning to address A, control transfers to the trampoline T.

### The Trampoline

The trampoline is a carefully crafted assembly routine that intercepts returns. It must preserve return values (the returning function may have placed values in registers like rax/rdx on x86_64 or x0-x7 on ARM64), query the shadow stack by calling `ghost_trampoline_handler(sp)` to get the original return address, then restore the saved registers and jump to the real return address.

On x86_64, the trampoline saves the return value registers onto the stack, aligns the stack to 16 bytes per the SysV ABI, calls the handler with the original stack pointer, then restores registers and jumps to the returned address.

On ARM64, the trampoline allocates space for and saves registers x0-x7 per AAPCS64, calls the handler, moves the returned address into the link register (x30), restores all saved registers, and branches to the original return address.

### Exception Handling (DWARF/LSDA)

C++ exceptions use stack unwinding to find catch blocks. Without proper metadata, exceptions thrown through a patched frame would corrupt the stack. GhostStack solves this by embedding **DWARF CFI (Call Frame Information)** and an **LSDA (Language Specific Data Area)** in the trampoline.

The CFI directives declare that the trampoline uses the C++ personality routine for exception handling, point to our custom LSDA, and mark the return address as "undefined" (since it's stored elsewhere in the shadow stack rather than on the real stack).

The LSDA defines a **catch-all handler** that calls `ghost_exception_handler()` to retrieve the original return address, pushes it onto the stack so the unwinder sees the correct return location, and calls `__cxa_rethrow` to continue unwinding through the original call stack. The LSDA's call site table covers the entire trampoline region and directs all exceptions to this landing pad.

### longjmp Detection

`longjmp()` bypasses normal return sequences, jumping directly to a `setjmp()` point. This leaves stale entries in the shadow stack. GhostStack detects this by comparing stack pointers: when a trampoline fires, the handler compares the current stack pointer against the expected value stored in the shadow stack entry. If they don't match, a longjmp must have occurred. The handler then searches backward through the shadow stack entries to find one with a matching stack pointer, skipping all the bypassed frames, and updates the tail pointer accordingly.

### Pointer Authentication (ARM64 PAC)

On ARM64 systems with PAC (Pointer Authentication Codes), return addresses contain cryptographic signatures in their upper bits. GhostStack strips these authentication bits before comparison using the `xpaclri` instruction, which clears the PAC bits from the link register without requiring the signing key.

### Thread Safety

Each thread has its own shadow stack via C++ `thread_local` storage. A wrapper class ensures cleanup on thread exit—its destructor calls `reset()` to restore original return addresses before the thread's stack is deallocated.

### Fork Safety

After `fork()`, the child process has a copy of the parent's shadow stack entries pointing to valid stack locations (virtual addresses are preserved). A handler registered via `pthread_atfork()` ensures the child process resets its shadow stack, restoring the original return addresses before any trampolines can fire in the forked process.

### Signal Safety

An atomic epoch counter guards against race conditions with signal handlers. The `reset()` function increments the epoch before modifying any state. The trampoline handler captures the epoch at entry and verifies it hasn't changed before returning; if a signal handler called `reset()` mid-operation, the epoch mismatch triggers an abort rather than returning a potentially corrupted address.

### Performance Characteristics

| Operation | Time Complexity | Notes |
|-----------|-----------------|-------|
| First capture | O(n) | Full stack walk via libunwind |
| Subsequent captures | O(k) | k = min(frames requested, depth) |
| Function return through trampoline | O(1) | Single shadow stack pop |
| longjmp through patched frames | O(m) | m = frames skipped by longjmp |
| Reset | O(n) | Restore n patched return addresses |

The key insight is that in profiling/tracing workloads, the same call stack is captured repeatedly (e.g., every allocation, every syscall). After the first O(n) capture, all subsequent captures from the same or deeper stack depth are effectively free.

## License

MIT License

