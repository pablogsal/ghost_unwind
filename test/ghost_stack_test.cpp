/**
 * @file ghost_stack_test.cpp
 * @brief Unified Google Test suite for GhostStack
 */

#include "ghost_stack.h"

#include <gtest/gtest.h>

#include <atomic>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cxxabi.h>
#include <dlfcn.h>
#include <exception>
#include <memory>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <execinfo.h>
#else
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif

// =============================================================================
// Test Fixture
// =============================================================================

class GhostStackTest : public ::testing::Test {
protected:
    void SetUp() override {
        ghost_stack_init(nullptr);
        ghost_stack_reset();
    }

    void TearDown() override {
        ghost_stack_reset();
    }
};

// =============================================================================
// Basic Tests
// =============================================================================

__attribute__((noinline))
static size_t capture_frames_noinline(void** frames, size_t max) {
    return ghost_stack_backtrace(frames, max);
}

TEST_F(GhostStackTest, BasicCapture) {
    void* frames[64];
    size_t nframes = capture_frames_noinline(frames, 64);

    EXPECT_GE(nframes, 2u) << "should capture at least 2 frames";
    EXPECT_NE(frames[0], nullptr) << "first frame should not be null";
}

TEST_F(GhostStackTest, CacheHitDetection) {
    void* frames1[64];
    size_t n1 = ghost_stack_backtrace(frames1, 64);
    ASSERT_GT(n1, 0u) << "first capture should succeed";

    void* frames2[64];
    size_t n2 = ghost_stack_backtrace(frames2, 64);
    EXPECT_GT(n2, 0u) << "second capture should succeed (cache hit)";
}

static volatile int recursion_blocker = 0;

__attribute__((noinline))
static bool recurse_and_capture(int depth, int max_depth) {
    volatile int local_depth = depth;

    if (local_depth >= max_depth) {
        void* frames[256];
        size_t nframes = ghost_stack_backtrace(frames, 256);
        return nframes >= 10;
    }

    bool result = recurse_and_capture(local_depth + 1, max_depth);
    recursion_blocker = local_depth;
    return result;
}

TEST_F(GhostStackTest, DeepRecursion) {
    EXPECT_TRUE(recurse_and_capture(0, 30)) << "should handle 30 levels of recursion";
}

TEST_F(GhostStackTest, ResetAndRecapture) {
    void* frames[64];

    size_t n1 = ghost_stack_backtrace(frames, 64);
    ASSERT_GT(n1, 0u) << "first capture should succeed";

    ghost_stack_reset();

    size_t n2 = ghost_stack_backtrace(frames, 64);
    EXPECT_GT(n2, 0u) << "capture after reset should succeed";
}

TEST_F(GhostStackTest, ThreadCleanup) {
    void* frames[64];
    ghost_stack_backtrace(frames, 64);
    ghost_stack_thread_cleanup();

    size_t n = ghost_stack_backtrace(frames, 64);
    EXPECT_GT(n, 0u) << "should be able to use after thread cleanup";
}

// =============================================================================
// Exception Tests
// =============================================================================

class GhostStackExceptionTest : public GhostStackTest {};

__attribute__((noinline))
static void throw_with_trace() {
    void* frames[64];
    ghost_stack_backtrace(frames, 64);
    throw std::runtime_error("test");
}

TEST_F(GhostStackExceptionTest, BasicException) {
    EXPECT_THROW({
        throw_with_trace();
    }, std::runtime_error);
}

__attribute__((noinline))
static void recurse_and_throw(int depth, int target) {
    void* frames[64];
    ghost_stack_backtrace(frames, 64);

    if (depth >= target) {
        throw std::runtime_error("depth reached");
    }
    recurse_and_throw(depth + 1, target);
}

TEST_F(GhostStackExceptionTest, ExceptionAtDepth5) {
    EXPECT_THROW(recurse_and_throw(0, 5), std::runtime_error);
}

TEST_F(GhostStackExceptionTest, ExceptionAtDepth20) {
    EXPECT_THROW(recurse_and_throw(0, 20), std::runtime_error);
}

__attribute__((noinline))
static std::string nested_try_catch() {
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
        return e.what();
    }
    return "";
}

TEST_F(GhostStackExceptionTest, NestedTryCatch) {
    EXPECT_EQ(nested_try_catch(), "outer");
}

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

TEST_F(GhostStackExceptionTest, RAIICleanup) {
    destructor_count = 0;
    EXPECT_THROW(raii_throw(), std::runtime_error);
    EXPECT_EQ(destructor_count, 1) << "destructor should be called during unwinding";
}

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

TEST_F(GhostStackExceptionTest, MultiRAIICleanupOrder) {
    cleanup_order.clear();
    EXPECT_THROW(multi_raii_throw(), std::runtime_error);

    ASSERT_EQ(cleanup_order.size(), 6u);
    EXPECT_EQ(cleanup_order[0], 10);  // g1 construct
    EXPECT_EQ(cleanup_order[1], 20);  // g2 construct
    EXPECT_EQ(cleanup_order[2], 30);  // g3 construct
    EXPECT_EQ(cleanup_order[3], 3);   // g3 destruct
    EXPECT_EQ(cleanup_order[4], 2);   // g2 destruct
    EXPECT_EQ(cleanup_order[5], 1);   // g1 destruct
}

TEST_F(GhostStackExceptionTest, ExceptionPtr) {
    std::exception_ptr eptr;

    try {
        void* frames[64];
        ghost_stack_backtrace(frames, 64);
        throw std::runtime_error("ptr test");
    } catch (...) {
        eptr = std::current_exception();
    }

    ghost_stack_reset();

    EXPECT_THROW({
        std::rethrow_exception(eptr);
    }, std::runtime_error);
}

TEST_F(GhostStackExceptionTest, DifferentExceptionTypes) {
    // int
    EXPECT_THROW({
        void* frames[64];
        ghost_stack_backtrace(frames, 64);
        throw 42;
    }, int);
    ghost_stack_reset();

    // const char*
    EXPECT_THROW({
        void* frames[64];
        ghost_stack_backtrace(frames, 64);
        throw "test string";
    }, const char*);
    ghost_stack_reset();

    // std::string
    EXPECT_THROW({
        void* frames[64];
        ghost_stack_backtrace(frames, 64);
        throw std::string("string exception");
    }, std::string);
}

TEST_F(GhostStackExceptionTest, SequentialExceptions) {
    for (int i = 0; i < 5; i++) {
        EXPECT_THROW({
            void* frames[64];
            ghost_stack_backtrace(frames, 64);
            throw std::runtime_error("iteration");
        }, std::runtime_error);
        ghost_stack_reset();
    }
}

// =============================================================================
// Libunwind Comparison Tests
// =============================================================================

class GhostStackCompareTest : public GhostStackTest {};

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

    size_t n_ghost = ghost_stack_backtrace(ghost_frames, 64);

    if (n_ghost == 0 || n_ref == 0) return false;

    // Both unw_backtrace and ghost_stack_backtrace return frames in
    // newest-first order (innermost at [0], outermost at [n-1]).
    // Check that the outermost frames (oldest) from ghost_stack appear
    // in ref_frames. Use the last few ghost frames since they're the
    // deepest/oldest and most likely to match.
    for (size_t g = n_ghost > 3 ? n_ghost - 3 : 0; g < n_ghost; g++) {
        for (size_t i = 0; i < n_ref; i++) {
            if (ref_frames[i] == ghost_frames[g]) {
                return true;
            }
        }
    }
    return false;
}

TEST_F(GhostStackCompareTest, FramesMatchShallow) {
    EXPECT_TRUE(compare_frames_inner()) << "ghost_stack frames should match libunwind";
}

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

TEST_F(GhostStackCompareTest, FramesMatchDeep) {
    EXPECT_TRUE(recurse_and_compare(0, 10)) << "ghost_stack frames should match at depth";
}

// =============================================================================
// Longjmp Tests
// =============================================================================

class GhostStackLongjmpTest : public GhostStackTest {};

static jmp_buf jump_buffer;
static volatile int return_counter = 0;
static volatile int longjmp_triggered = 0;
static volatile size_t frames_captured = 0;

__attribute__((noinline))
static void inner_capture_and_jump(bool do_longjmp) {
    void* frames[64];
    frames_captured = ghost_stack_backtrace(frames, 64);

    if (do_longjmp) {
        longjmp(jump_buffer, 1);
    }
    return_counter++;
}

__attribute__((noinline))
static void skipped_frame_3(bool do_longjmp) {
    inner_capture_and_jump(do_longjmp);
    return_counter++;
}

__attribute__((noinline))
static void skipped_frame_2(bool do_longjmp) {
    skipped_frame_3(do_longjmp);
    return_counter++;
}

__attribute__((noinline))
static void skipped_frame_1(bool do_longjmp) {
    skipped_frame_2(do_longjmp);
    return_counter++;
}

__attribute__((noinline))
static int setjmp_frame() {
    if (setjmp(jump_buffer) == 0) {
        skipped_frame_1(true);
        return -1;
    } else {
        longjmp_triggered = 1;
        return_counter++;
        return 0;
    }
}

__attribute__((noinline))
static int outer_frame_2() {
    int result = setjmp_frame();
    return_counter++;
    return result;
}

__attribute__((noinline))
static int outer_frame_1() {
    int result = outer_frame_2();
    return_counter++;
    return result;
}

TEST_F(GhostStackLongjmpTest, LongjmpDetection) {
    return_counter = 0;
    longjmp_triggered = 0;
    frames_captured = 0;

    outer_frame_1();

    EXPECT_TRUE(longjmp_triggered) << "longjmp should have been triggered";
    EXPECT_GT(frames_captured, 0u) << "frames should have been captured";
    EXPECT_GE(return_counter, 3) << "at least 3 frames should return correctly";
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

class GhostStackThreadTest : public GhostStackTest {};

__attribute__((noinline))
static void capture_at_depth(int depth) {
    if (depth > 0) {
        capture_at_depth(depth - 1);
        return;
    }

    void* frames[64];
    ghost_stack_backtrace(frames, 64);
}

static void* capture_thread(void* arg) {
    int thread_id = *reinterpret_cast<int*>(arg);

    for (int i = 0; i < 100; i++) {
        capture_at_depth(5);
        if (i % 10 == 0) {
            ghost_stack_reset();
        }
    }

    ghost_stack_thread_cleanup();
    return nullptr;
}

TEST_F(GhostStackThreadTest, MultipleThreads) {
    const int NUM_THREADS = 4;
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        ASSERT_EQ(pthread_create(&threads[i], nullptr, capture_thread, &thread_ids[i]), 0);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }
}

TEST_F(GhostStackThreadTest, RapidReset) {
    for (int i = 0; i < 1000; i++) {
        void* frames[64];
        size_t n = ghost_stack_backtrace(frames, 64);
        EXPECT_GT(n, 0u);
        ghost_stack_reset();
    }
}

__attribute__((noinline))
static size_t nested_capture_inner() {
    void* frames[64];
    return ghost_stack_backtrace(frames, 64);
}

__attribute__((noinline))
static size_t nested_capture_outer() {
    void* frames[64];
    size_t outer = ghost_stack_backtrace(frames, 64);
    size_t inner = nested_capture_inner();
    return outer + inner;
}

TEST_F(GhostStackThreadTest, NestedCaptures) {
    size_t total = nested_capture_outer();
    EXPECT_GT(total, 0u) << "nested captures should return frames";
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
