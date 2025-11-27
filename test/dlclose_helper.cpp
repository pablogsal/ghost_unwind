/**
 * GhostStack dlclose Test Helper Library
 *
 * This is a shared library that embeds ghost_stack.
 * Used to test that trampolines are properly restored when the library
 * is unloaded via dlclose().
 */

#include "ghost_stack.h"
#include <cstdio>

// Export functions for dlsym
extern "C" {

__attribute__((noinline))
static void helper_depth_3(void** frames, size_t* count) {
    *count = ghost_stack_backtrace(frames, 64);
    printf("[dlclose_helper] Captured %zu frames at depth 3\n", *count);
}

__attribute__((noinline))
static void helper_depth_2(void** frames, size_t* count) {
    helper_depth_3(frames, count);
}

__attribute__((noinline))
static void helper_depth_1(void** frames, size_t* count) {
    helper_depth_2(frames, count);
}

__attribute__((visibility("default")))
void dlclose_helper_init(void) {
    ghost_stack_init(nullptr);
    printf("[dlclose_helper] Initialized\n");
}

__attribute__((visibility("default")))
size_t dlclose_helper_capture(void** frames, size_t /* max_frames */) {
    size_t count = 0;
    helper_depth_1(frames, &count);
    return count;
}

__attribute__((visibility("default")))
void dlclose_helper_reset(void) {
    ghost_stack_reset();
    printf("[dlclose_helper] Reset called\n");
}

__attribute__((visibility("default")))
void dlclose_helper_cleanup(void) {
    ghost_stack_thread_cleanup();
    printf("[dlclose_helper] Thread cleanup called\n");
}

} // extern "C"
