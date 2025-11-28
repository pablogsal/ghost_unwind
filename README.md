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

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          First Capture (Slow Path)                      │
├─────────────────────────────────────────────────────────────────────────┤
│  1. libunwind walks the stack, collecting IPs and return address locs   │
│  2. For each frame, save: {IP, original_ret_addr, stack_location, SP}   │
│  3. Patch each return address on stack → point to trampoline            │
│  4. Return IPs to caller                                                │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│                       Subsequent Captures (Fast Path)                   │
├─────────────────────────────────────────────────────────────────────────┤
│  1. Check trampolines_installed_ flag                                   │
│  2. memcpy IPs from shadow stack entries → output buffer                │
│  3. Return immediately (no stack walk, no DWARF parsing)                │
└─────────────────────────────────────────────────────────────────────────┘
```

### Shadow Stack Data Structure

Each entry in the shadow stack stores:

```c
struct StackEntry {
    uintptr_t ip;              // Instruction pointer (what to report to caller)
    uintptr_t return_address;  // Original return address (what we replaced)
    uintptr_t* location;       // Memory address on stack where ret addr lives
    uintptr_t stack_pointer;   // SP at capture time (for longjmp detection)
};
```

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

The trampoline is a carefully crafted assembly routine that intercepts returns. It must:

1. **Preserve return values** — The returning function may have placed values in registers (rax/rdx on x86_64, x0-x7 on ARM64)
2. **Query the shadow stack** — Call `ghost_trampoline_handler(sp)` to get the original return address
3. **Restore and continue** — Pop saved registers and jump to the real return address

**x86_64 trampoline flow:**
```asm
ghost_ret_trampoline:
    push rax                    ; Save return value registers
    push rdx
    push rcx
    sub rsp, 8                  ; Align stack to 16 bytes (SysV ABI requirement)

    mov rdi, rsp
    add rdi, 32                 ; Pass original SP to handler
    call ghost_trampoline_handler  ; Returns real ret addr in rax

    mov rsi, rax                ; Save return address
    add rsp, 8                  ; Remove alignment
    pop rcx                     ; Restore return values
    pop rdx
    pop rax
    jmp rsi                     ; Jump to original return address
```

**ARM64 trampoline flow:**
```asm
ghost_ret_trampoline:
    sub sp, sp, #64             ; Allocate space for x0-x7
    stp x0, x1, [sp, 0]         ; Save return value registers (AAPCS64)
    stp x2, x3, [sp, 16]
    stp x4, x5, [sp, 32]
    stp x6, x7, [sp, 48]

    mov x0, sp
    add x0, x0, #64             ; Pass original SP
    bl ghost_trampoline_handler ; Returns real ret addr in x0

    mov x30, x0                 ; Move to link register
    ldp x0, x1, [sp, 0]         ; Restore return values
    ldp x2, x3, [sp, 16]
    ldp x4, x5, [sp, 32]
    ldp x6, x7, [sp, 48]
    add sp, sp, #64
    br x30                      ; Branch to original return address
```

### Exception Handling (DWARF/LSDA)

C++ exceptions use stack unwinding to find catch blocks. Without proper metadata, exceptions thrown through a patched frame would corrupt the stack. GhostStack solves this by embedding **DWARF CFI (Call Frame Information)** and an **LSDA (Language Specific Data Area)** in the trampoline:

```
.cfi_startproc
.cfi_personality 0x9b, DW.ref.__gxx_personality_v0   ; Use C++ personality
.cfi_lsda 0x1b, .LLSDA0                              ; Point to our LSDA
.cfi_undefined rip                                    ; Return addr is "elsewhere"
```

The LSDA defines a **catch-all handler** that:
1. Calls `ghost_exception_handler()` to retrieve the original return address
2. Pushes it onto the stack so the unwinder sees the correct return location
3. Calls `__cxa_rethrow` to continue unwinding through the original call stack

```
LSDA structure:
┌─────────────────────────────────────────┐
│ @LPStart encoding: omit                 │
│ @TType encoding: indirect pcrel sdata4  │
├─────────────────────────────────────────┤
│ Call site table:                        │
│   Region: .LEHB0 to .LEHE0             │
│   Landing pad: .L3                      │
│   Action: catch-all (type 0)            │
└─────────────────────────────────────────┘
```

### longjmp Detection

`longjmp()` bypasses normal return sequences, jumping directly to a `setjmp()` point. This leaves stale entries in the shadow stack. GhostStack detects this by comparing stack pointers:

```c
uintptr_t on_ret_trampoline(uintptr_t sp) {
    auto& entry = entries_[tail];

    // If SP doesn't match, longjmp occurred—search backward for matching frame
    if (entry.stack_pointer != sp) {
        for (size_t i = tail; i > 0; --i) {
            if (entries_[i - 1].stack_pointer == sp) {
                // Found it! Skip the bypassed frames
                tail_.store(i - 1);
                break;
            }
        }
    }
    return entries_[tail].return_address;
}
```

### Pointer Authentication (ARM64 PAC)

On ARM64 systems with PAC (Pointer Authentication Codes), return addresses contain cryptographic signatures in their upper bits. GhostStack strips these before comparison using the `xpaclri` instruction:

```c
static inline uintptr_t ptrauth_strip(uintptr_t val) {
    uint64_t ret;
    asm volatile(
        "mov x30, %1\n\t"
        "xpaclri\n\t"        // Strip PAC from LR
        "mov %0, x30\n\t"
        : "=r"(ret) : "r"(val) : "x30");
    return ret;
}
```

### Thread Safety

Each thread has its own shadow stack via C++ `thread_local` storage:

```c
static thread_local ThreadLocalInstance t_instance;
```

The `ThreadLocalInstance` wrapper ensures cleanup on thread exit—the destructor calls `reset()` to restore original return addresses before the thread's stack is deallocated.

### Fork Safety

After `fork()`, the child process has a copy of the parent's shadow stack entries pointing to valid stack locations (virtual addresses are preserved). A `pthread_atfork()` handler ensures the child resets its shadow stack:

```c
static void fork_child_handler() {
    if (t_instance.ptr) {
        t_instance.ptr->reset();  // Restore original return addresses
    }
}
// Registered via: pthread_atfork(NULL, NULL, fork_child_handler);
```

### Signal Safety

An epoch counter guards against race conditions with signal handlers:

```c
std::atomic<uint64_t> epoch_{0};

void reset() {
    epoch_.fetch_add(1);  // Invalidate in-flight operations
    // ... restore return addresses ...
}

uintptr_t on_ret_trampoline(uintptr_t sp) {
    uint64_t current_epoch = epoch_.load();
    // ... do work ...
    if (epoch_.load() != current_epoch) {
        abort();  // Reset was called mid-operation
    }
    return entries_[tail].return_address;
}
```

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

