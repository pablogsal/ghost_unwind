/**
 * Example: LD_PRELOAD library that traces read() calls with stack traces
 *
 * Usage:
 *   LD_PRELOAD=./libread_tracer.so ./your_program
 */

#define _GNU_SOURCE
#include "ghost_stack.h"

#include <ctime>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>
#include <cxxabi.h>

// Recursion guard (thread-local)
static thread_local bool in_hook = false;

// Real read function
typedef ssize_t (*real_read_t)(int fd, void *buf, size_t count);
static real_read_t real_read = nullptr;

// Mutex for output
static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

// Simple symbolizer for this example
static void print_symbol(void* addr) {
    Dl_info info;
    if (dladdr(addr, &info) && info.dli_sname) {
        int status;
        char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
        if (status == 0 && demangled) {
            printf("    %p <%s+0x%lx>\n", addr, demangled,
                   (char*)addr - (char*)info.dli_saddr);
            free(demangled);
        } else {
            printf("    %p <%s+0x%lx>\n", addr, info.dli_sname,
                   (char*)addr - (char*)info.dli_saddr);
        }
    } else {
        printf("    %p <unknown>\n", addr);
    }
}

// Initialize on library load
__attribute__((constructor))
static void init() {
    real_read = (real_read_t)dlsym(RTLD_NEXT, "read");
    if (!real_read) {
        fprintf(stderr, "Failed to get real read(): %s\n", dlerror());
        _exit(1);
    }

    // Initialize GhostStack with default unwinder
    ghost_stack_init(nullptr);
}

// Intercepted read()
extern "C" ssize_t read(int fd, void *buf, size_t count) {
    if (!real_read) {
        init();
    }

    // Avoid recursion from internal calls
    if (in_hook) {
        return real_read(fd, buf, count);
    }
    in_hook = true;

    // Capture stack trace
    void* frames[64];
    size_t nframes = ghost_stack_backtrace(frames, 64);

    // Call real read
    ssize_t result = real_read(fd, buf, count);

    // Print trace
    pthread_mutex_lock(&print_mutex);
    auto now = std::time(nullptr);
    printf("=== read(fd=%d, count=%zu) = %zd at %s", fd, count, result, std::ctime(&now));
    printf("Stack trace (%zu frames):\n", nframes);
    for (size_t i = 0; i < nframes; i++) {
        print_symbol(frames[i]);
    }
    printf("===\n\n");
    pthread_mutex_unlock(&print_mutex);

    in_hook = false;
    return result;
}
