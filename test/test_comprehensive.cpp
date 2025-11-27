/**
 * @file test_comprehensive.cpp
 * @brief Comprehensive test suite for GhostStack C API
 *
 * Tests:
 * 1. Basic stack capture
 * 2. Multiple captures (cache behavior)
 * 3. Deep recursion handling
 * 4. Exception handling at different depths
 * 5. Reset functionality
 */

#include "ghost_stack.h"

#include <cstdio>
#include <cstdlib>
#include <cxxabi.h>
#include <dlfcn.h>
#include <exception>
#include <stdexcept>
#include <vector>
#include <string>

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;
static std::vector<std::string> failed_tests;

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
            failed_tests.push_back(name); \
        } \
    } while(0)

//==============================================================================
// Test: Basic Capture
//==============================================================================

__attribute__((noinline))
static bool test_basic_capture_inner() {
    void* frames[64];
    size_t nframes = ghost_stack_backtrace(frames, 64);

    TEST_ASSERT(nframes >= 2, "should capture at least 2 frames");
    TEST_ASSERT(frames[0] != nullptr, "first frame should not be null");

    return true;
}

static bool test_basic_capture() {
    return test_basic_capture_inner();
}

//==============================================================================
// Test: Multiple Captures (Cache)
//==============================================================================

__attribute__((noinline))
static bool test_cache_inner() {
    void* frames1[64];
    size_t n1 = ghost_stack_backtrace(frames1, 64);
    TEST_ASSERT(n1 > 0, "first capture should succeed");

    void* frames2[64];
    size_t n2 = ghost_stack_backtrace(frames2, 64);
    TEST_ASSERT(n2 > 0, "second capture should succeed");

    // Both should return frames (cache should work)
    return true;
}

static bool test_cache() {
    return test_cache_inner();
}

//==============================================================================
// Test: Deep Recursion
//==============================================================================

static volatile int recursion_blocker = 0;

__attribute__((noinline))
static bool recurse_and_capture(int depth, int max_depth) {
    volatile int local_depth = depth;

    if (local_depth >= max_depth) {
        void* frames[256];
        size_t nframes = ghost_stack_backtrace(frames, 256);

        if (nframes < 10) {
            fprintf(stderr, "Expected at least 10 frames, got %zu\n", nframes);
            return false;
        }
        return true;
    }

    bool result = recurse_and_capture(local_depth + 1, max_depth);
    recursion_blocker = local_depth;  // Prevent TCO
    return result;
}

static bool test_deep_recursion() {
    return recurse_and_capture(0, 30);
}

//==============================================================================
// Test: Exception at Depth
//==============================================================================

__attribute__((noinline))
static void throw_at_depth(int depth, int target) {
    if (depth >= target) {
        void* frames[64];
        ghost_stack_backtrace(frames, 64);
        throw std::runtime_error("test exception");
    }
    throw_at_depth(depth + 1, target);
}

static bool test_exception_depth_3() {
    try {
        throw_at_depth(0, 3);
        return false;  // Should not reach
    } catch (const std::runtime_error&) {
        return true;  // Exception caught correctly
    }
}

static bool test_exception_depth_10() {
    try {
        throw_at_depth(0, 10);
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

//==============================================================================
// Test: Reset and Recapture
//==============================================================================

__attribute__((noinline))
static bool test_reset_inner() {
    void* frames[64];

    // First capture
    size_t n1 = ghost_stack_backtrace(frames, 64);
    TEST_ASSERT(n1 > 0, "first capture should succeed");

    // Reset
    ghost_stack_reset();

    // Capture again
    size_t n2 = ghost_stack_backtrace(frames, 64);
    TEST_ASSERT(n2 > 0, "capture after reset should succeed");

    return true;
}

static bool test_reset() {
    return test_reset_inner();
}

//==============================================================================
// Test: Capture Without Trampolines (raw unwinding)
//==============================================================================

static bool test_raw_unwind() {
    // Just verify basic unwinding works
    void* frames[64];
    size_t n = ghost_stack_backtrace(frames, 64);
    return n > 0;
}

//==============================================================================
// Test: Multiple Exceptions
//==============================================================================

__attribute__((noinline))
static bool nested_exception_test(int iterations) {
    if (iterations <= 0) return true;

    void* frames[64];
    ghost_stack_backtrace(frames, 64);

    try {
        throw std::runtime_error("nested");
    } catch (...) {
        ghost_stack_reset();
        return nested_exception_test(iterations - 1);
    }
}

static bool test_multiple_exceptions() {
    return nested_exception_test(5);
}

//==============================================================================
// Test: Thread Cleanup
//==============================================================================

static bool test_thread_cleanup() {
    void* frames[64];
    ghost_stack_backtrace(frames, 64);
    ghost_stack_thread_cleanup();

    // Should be able to use again
    size_t n = ghost_stack_backtrace(frames, 64);
    return n > 0;
}

//==============================================================================
// Main
//==============================================================================

int main() {
    printf("=== GhostStack Comprehensive Test Suite ===\n\n");

    ghost_stack_init(nullptr);

    RUN_TEST("Basic Capture", test_basic_capture);
    RUN_TEST("Cache Hit Detection", test_cache);
    RUN_TEST("Deep Recursion (30 levels)", test_deep_recursion);
    RUN_TEST("Exception at Depth 3", test_exception_depth_3);
    RUN_TEST("Exception at Depth 10", test_exception_depth_10);
    RUN_TEST("Reset and Recapture", test_reset);
    RUN_TEST("Raw Unwind", test_raw_unwind);
    RUN_TEST("Multiple Exceptions", test_multiple_exceptions);
    RUN_TEST("Thread Cleanup", test_thread_cleanup);

    printf("\n======================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    if (!failed_tests.empty()) {
        printf("\nFailed tests:\n");
        for (const auto& name : failed_tests) {
            printf("  - %s\n", name.c_str());
        }
    }
    printf("======================================\n");

    ghost_stack_thread_cleanup();
    return tests_failed > 0 ? 1 : 0;
}
