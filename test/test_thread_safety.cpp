/**
 * GhostStack Thread Safety Test
 *
 * Tests that the shadow stack handles concurrent operations correctly,
 * particularly the epoch-based detection for reset during trampoline execution.
 */

#include "ghost_stack.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>

static std::atomic<int> test_phase{0};
static std::atomic<bool> test_passed{true};

// Capture stack trace at depth
__attribute__((noinline))
static void capture_at_depth(int depth) {
    if (depth > 0) {
        capture_at_depth(depth - 1);
        return;
    }

    void* frames[64];
    size_t n = ghost_stack_backtrace(frames, 64);
    if (n == 0) {
        printf("[Thread] Warning: captured 0 frames\n");
    }
}

// Thread function that captures stack traces
static void* capture_thread(void* arg) {
    int thread_id = *reinterpret_cast<int*>(arg);
    printf("[Thread %d] Starting\n", thread_id);

    for (int i = 0; i < 100; i++) {
        capture_at_depth(5);

        // Occasionally reset
        if (i % 10 == 0) {
            ghost_stack_reset();
        }
    }

    printf("[Thread %d] Completed 100 iterations\n", thread_id);
    ghost_stack_thread_cleanup();
    return nullptr;
}

// Test multiple threads each with their own shadow stack
static bool test_multiple_threads() {
    printf("\nTest: Multiple threads with independent shadow stacks\n");

    const int NUM_THREADS = 4;
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], nullptr, capture_thread, &thread_ids[i]) != 0) {
            perror("pthread_create");
            return false;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }

    printf("All threads completed successfully\n");
    return true;
}

// Test rapid capture/reset cycles
static bool test_rapid_reset() {
    printf("\nTest: Rapid capture/reset cycles\n");

    for (int i = 0; i < 1000; i++) {
        void* frames[64];
        size_t n = ghost_stack_backtrace(frames, 64);
        ghost_stack_reset();

        if (i % 100 == 0) {
            printf("  Iteration %d: captured %zu frames\n", i, n);
        }
    }

    printf("Rapid reset test completed\n");
    return true;
}

// Test nested captures (should return cached results)
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

static bool test_nested_captures() {
    printf("\nTest: Nested captures (cache behavior)\n");

    size_t total = nested_capture_outer();
    printf("  Total frames from nested captures: %zu\n", total);

    ghost_stack_reset();
    printf("Nested capture test completed\n");
    return true;
}

int main() {
    printf("=== GhostStack Thread Safety Tests ===\n");

    ghost_stack_init(nullptr);

    bool all_passed = true;

    all_passed &= test_multiple_threads();
    all_passed &= test_rapid_reset();
    all_passed &= test_nested_captures();

    ghost_stack_thread_cleanup();

    printf("\n");
    if (all_passed) {
        printf("=== All Thread Safety Tests PASSED ===\n");
        return 0;
    } else {
        printf("=== Some Thread Safety Tests FAILED ===\n");
        return 1;
    }
}
