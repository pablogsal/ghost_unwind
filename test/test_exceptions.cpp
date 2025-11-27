/**
 * @file test_exceptions.cpp
 * @brief Exception handling tests for GhostStack
 *
 * Tests that C++ exceptions propagate correctly through patched frames.
 */

#include "ghost_stack.h"

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>

static int tests_passed = 0;
static int tests_failed = 0;
static std::vector<std::string> failed_tests;

#define RUN_TEST(name, func) \
    do { \
        printf("Running: %s... ", name); \
        fflush(stdout); \
        ghost_stack_reset(); \
        bool result = false; \
        try { result = func(); } catch (...) { result = false; } \
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
// Test: Basic Exception
//==============================================================================

__attribute__((noinline))
static void throw_with_trace() {
    void* frames[64];
    ghost_stack_backtrace(frames, 64);
    throw std::runtime_error("test");
}

static bool test_basic_exception() {
    try {
        throw_with_trace();
        return false;
    } catch (const std::runtime_error& e) {
        return std::string(e.what()) == "test";
    }
}

//==============================================================================
// Test: Exception at Various Depths
//==============================================================================

__attribute__((noinline))
static void recurse_and_throw(int depth, int target) {
    void* frames[64];
    ghost_stack_backtrace(frames, 64);

    if (depth >= target) {
        throw std::runtime_error("depth reached");
    }
    recurse_and_throw(depth + 1, target);
}

static bool test_exception_depth_5() {
    try {
        recurse_and_throw(0, 5);
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

static bool test_exception_depth_20() {
    try {
        recurse_and_throw(0, 20);
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

//==============================================================================
// Test: Nested Try-Catch
//==============================================================================

__attribute__((noinline))
static bool nested_try_catch() {
    void* frames[64];

    try {
        ghost_stack_backtrace(frames, 64);
        try {
            ghost_stack_backtrace(frames, 64);
            throw std::runtime_error("inner");
        } catch (const std::runtime_error&) {
            ghost_stack_backtrace(frames, 64);
            throw std::runtime_error("outer");
        }
    } catch (const std::runtime_error& e) {
        return std::string(e.what()) == "outer";
    }
    return false;
}

static bool test_nested_try_catch() {
    return nested_try_catch();
}

//==============================================================================
// Test: RAII Cleanup on Exception
//==============================================================================

static int destructor_count = 0;

struct RAIIGuard {
    RAIIGuard() { destructor_count = 0; }
    ~RAIIGuard() { destructor_count++; }
};

__attribute__((noinline))
static void raii_throw() {
    RAIIGuard guard;
    void* frames[64];
    ghost_stack_backtrace(frames, 64);
    throw std::runtime_error("raii test");
}

static bool test_raii_cleanup() {
    destructor_count = 0;
    try {
        raii_throw();
        return false;
    } catch (...) {
        return destructor_count == 1;
    }
}

//==============================================================================
// Test: Multiple RAII Objects
//==============================================================================

static std::vector<int> cleanup_order;

struct OrderedGuard {
    int id;
    OrderedGuard(int i) : id(i) { cleanup_order.push_back(id * 10); }
    ~OrderedGuard() { cleanup_order.push_back(id); }
};

__attribute__((noinline))
static void multi_raii_throw() {
    OrderedGuard g1(1);
    void* frames[64];
    ghost_stack_backtrace(frames, 64);
    OrderedGuard g2(2);
    ghost_stack_backtrace(frames, 64);
    OrderedGuard g3(3);
    throw std::runtime_error("multi raii");
}

static bool test_multi_raii_cleanup() {
    cleanup_order.clear();
    try {
        multi_raii_throw();
        return false;
    } catch (...) {
        // Order: construct 1, 2, 3, then destruct 3, 2, 1
        if (cleanup_order.size() != 6) return false;
        if (cleanup_order[0] != 10) return false;  // g1 construct
        if (cleanup_order[1] != 20) return false;  // g2 construct
        if (cleanup_order[2] != 30) return false;  // g3 construct
        if (cleanup_order[3] != 3) return false;   // g3 destruct
        if (cleanup_order[4] != 2) return false;   // g2 destruct
        if (cleanup_order[5] != 1) return false;   // g1 destruct
        return true;
    }
}

//==============================================================================
// Test: exception_ptr
//==============================================================================

static bool test_exception_ptr() {
    std::exception_ptr eptr;

    try {
        void* frames[64];
        ghost_stack_backtrace(frames, 64);
        throw std::runtime_error("ptr test");
    } catch (...) {
        eptr = std::current_exception();
    }

    ghost_stack_reset();

    try {
        std::rethrow_exception(eptr);
        return false;
    } catch (const std::runtime_error& e) {
        return std::string(e.what()) == "ptr test";
    }
}

//==============================================================================
// Test: Different Exception Types
//==============================================================================

static bool test_different_types() {
    // int
    try {
        void* frames[64];
        ghost_stack_backtrace(frames, 64);
        throw 42;
    } catch (int v) {
        if (v != 42) return false;
    }
    ghost_stack_reset();

    // const char*
    try {
        void* frames[64];
        ghost_stack_backtrace(frames, 64);
        throw "test string";
    } catch (const char* s) {
        if (std::string(s) != "test string") return false;
    }
    ghost_stack_reset();

    // std::string
    try {
        void* frames[64];
        ghost_stack_backtrace(frames, 64);
        throw std::string("string exception");
    } catch (const std::string& s) {
        if (s != "string exception") return false;
    }
    ghost_stack_reset();

    return true;
}

//==============================================================================
// Test: Sequential Exceptions
//==============================================================================

static bool test_sequential_exceptions() {
    for (int i = 0; i < 5; i++) {
        try {
            void* frames[64];
            ghost_stack_backtrace(frames, 64);
            throw std::runtime_error("iteration");
        } catch (...) {
            ghost_stack_reset();
        }
    }
    return true;
}

//==============================================================================
// Main
//==============================================================================

int main() {
    printf("=== GhostStack Exception Tests ===\n\n");

    ghost_stack_init(nullptr);

    RUN_TEST("Basic Exception", test_basic_exception);
    RUN_TEST("Exception at Depth 5", test_exception_depth_5);
    RUN_TEST("Exception at Depth 20", test_exception_depth_20);
    RUN_TEST("Nested Try-Catch", test_nested_try_catch);
    RUN_TEST("RAII Cleanup on Exception", test_raii_cleanup);
    RUN_TEST("Multiple RAII Cleanup Order", test_multi_raii_cleanup);
    RUN_TEST("std::exception_ptr", test_exception_ptr);
    RUN_TEST("Different Exception Types", test_different_types);
    RUN_TEST("Sequential Exceptions", test_sequential_exceptions);

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
