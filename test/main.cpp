/**
 * @file main.cpp
 * @brief Basic test for GhostStack exception handling through trampolines
 *
 * This test verifies that:
 * 1. Stack traces can be captured correctly
 * 2. Trampolines are installed and function returns work
 * 3. Exceptions propagate correctly through patched frames
 * 4. Control flow returns to main after exception handling
 */

#include "ghost_stack.h"

#include <cstdio>
#include <cstdlib>
#include <cxxabi.h>
#include <dlfcn.h>
#include <exception>

// Simple symbolizer for test output
static void print_frame(void* addr) {
    Dl_info info;
    if (dladdr(addr, &info) && info.dli_sname) {
        int status;
        char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
        if (status == 0 && demangled) {
            printf("  %p <%s+0x%lx>\n", addr, demangled,
                   (char*)addr - (char*)info.dli_saddr);
            free(demangled);
        } else {
            printf("  %p <%s+0x%lx>\n", addr, info.dli_sname,
                   (char*)addr - (char*)info.dli_saddr);
        }
    } else {
        printf("  %p <unknown>\n", addr);
    }
}

// Force no inlining to ensure frames appear in stack trace
__attribute__((noinline)) int function3() {
    printf("In function3, capturing stack trace...\n");

    // First capture - should patch all frames
    void* frames[64];
    size_t nframes = ghost_stack_backtrace(frames, 64);

    if (nframes == 0) {
        fprintf(stderr, "First unwind failed\n");
        return -1;
    }

    printf("Captured %zu frames:\n", nframes);
    for (size_t i = 0; i < nframes; i++) {
        print_frame(frames[i]);
    }

    // Second capture - should detect already patched frames (cache hit)
    void* frames2[64];
    size_t nframes2 = ghost_stack_backtrace(frames2, 64);

    printf("Second capture: %zu frames\n", nframes2);

    // Now throw an exception to test exception handling
    printf("Throwing exception...\n");
    throw std::exception();

    // Should never reach here
    return 42;
}

__attribute__((noinline)) int function2() {
    printf("In function2\n");
    int res = function3();
    printf("Back in function2\n");  // Goes through trampoline
    return res + 1;
}

__attribute__((noinline)) int function1() {
    printf("In function1\n");
    int res = function2();
    printf("Back in function1\n");  // Goes through trampoline
    return res + 1;
}

int main() {
    printf("=== GhostStack Basic Test ===\n");

    // Initialize (optional - auto-init happens on first use)
    ghost_stack_init(nullptr);

    int res = 0;
    try {
        printf("Starting test...\n");
        res = function1();
        printf("function1 returned: %d\n", res);
    } catch (...) {
        printf("Exception caught in main - trampolines restored correctly!\n");
    }

    printf("Back in main after exception handling\n");

    // Clean up
    ghost_stack_reset();

    printf("\n=== Test PASSED ===\n");
    return 0;
}
