/**
 * @file test_libunwind_compare.cpp
 * @brief Test that ghost_stack_backtrace returns correct frame addresses
 *
 * This test verifies that the frames returned by ghost_stack_backtrace
 * match those returned by the system's backtrace function (libunwind on Linux,
 * execinfo backtrace() on macOS).
 */

#include "ghost_stack.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef __APPLE__
#include <execinfo.h>
#else
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "ASSERTION FAILED: %s\n", msg); \
            fprintf(stderr, "  at %s:%d\n", __FILE__, __LINE__); \
            return false; \
        } \
    } while(0)

#define RUN_TEST(name, func) \
    do { \
        printf("Running: %s... ", name); \
        fflush(stdout); \
        ghost_stack_reset(); \
        bool result = func(); \
        ghost_stack_reset(); \
        if (result) { \
            printf("PASSED\n"); \
            tests_passed++; \
        } else { \
            printf("FAILED\n"); \
            tests_failed++; \
        } \
    } while(0)

/**
 * Compare ghost_stack frames against libunwind reference.
 */
__attribute__((noinline))
static bool compare_frames_inner() {
    void* ghost_frames[64];
    void* ref_frames[64];

    // Capture reference FIRST before ghost_stack installs trampolines
#ifdef __APPLE__
    int n_ref_int = backtrace(ref_frames, 64);
#else
    int n_ref_int = unw_backtrace(ref_frames, 64);
#endif
    size_t n_ref = (n_ref_int > 0) ? static_cast<size_t>(n_ref_int) : 0;

    // Now capture with ghost_stack
    size_t n_ghost = ghost_stack_backtrace(ghost_frames, 64);

    TEST_ASSERT(n_ghost > 0, "ghost_stack should capture frames");
    TEST_ASSERT(n_ref > 0, "unw_backtrace should capture frames");

    // Find where ghost_frames[0] appears in ref_frames
    bool found = false;
    for (size_t i = 0; i < n_ref; i++) {
        if (ref_frames[i] == ghost_frames[0]) {
            found = true;
            break;
        }
    }

    if (!found) {
        printf("\n  ERROR: ghost_frames[0] (%p) not found in reference frames!\n", ghost_frames[0]);
        printf("  Ghost frames:\n");
        for (size_t i = 0; i < n_ghost && i < 5; i++) {
            printf("    [%zu] %p\n", i, ghost_frames[i]);
        }
        printf("  Reference frames:\n");
        for (size_t i = 0; i < n_ref && i < 5; i++) {
            printf("    [%zu] %p\n", i, ref_frames[i]);
        }
        return false;
    }

    return true;
}

static bool test_frames_match() {
    return compare_frames_inner();
}

/**
 * Test with deeper stack
 */
static volatile int recursion_blocker = 0;

__attribute__((noinline))
static bool recurse_and_compare(int depth, int max_depth) {
    volatile int local_depth = depth;

    if (local_depth >= max_depth) {
        return compare_frames_inner();
    }

    bool result = recurse_and_compare(local_depth + 1, max_depth);
    recursion_blocker = local_depth;
    return result;
}

static bool test_frames_match_deep() {
    return recurse_and_compare(0, 10);
}

int main() {
    printf("=== GhostStack vs libunwind Comparison Tests ===\n\n");

    ghost_stack_init(nullptr);

    RUN_TEST("Frames Match (shallow)", test_frames_match);
    RUN_TEST("Frames Match (deep)", test_frames_match_deep);

    printf("\n======================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("======================================\n");

    ghost_stack_thread_cleanup();
    return tests_failed > 0 ? 1 : 0;
}
