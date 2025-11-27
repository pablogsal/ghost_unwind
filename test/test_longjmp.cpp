/**
 * GhostStack longjmp Test
 *
 * Tests that the shadow stack correctly handles longjmp by detecting
 * skipped frames and restoring their original return addresses.
 *
 * Test strategy:
 * 1. Create a deep call stack with setjmp at a middle point
 * 2. Capture stack trace (installs trampolines on all frames)
 * 3. longjmp back, skipping several frames
 * 4. Return normally through remaining frames WITH trampolines still active
 * 5. If longjmp detection works, we return correctly; if not, we crash or corrupt
 */

#include "ghost_stack.h"
#include <csetjmp>
#include <cstdio>
#include <cstdlib>

static jmp_buf jump_buffer;
static volatile int return_counter = 0;
static volatile int longjmp_triggered = 0;
static volatile size_t frames_captured = 0;

/**
 * Innermost function - captures stack and optionally does longjmp.
 * After longjmp, the frames between here and setjmp are "skipped" -
 * the shadow stack must detect this via SP mismatch and restore their
 * original return addresses.
 */
__attribute__((noinline))
static void inner_capture_and_jump(bool do_longjmp) {
    void* frames[64];
    frames_captured = ghost_stack_backtrace(frames, 64);
    printf("  [inner] Captured %zu frames\n", frames_captured);

    if (do_longjmp) {
        printf("  [inner] About to longjmp (skipping frames)...\n");
        longjmp(jump_buffer, 1);
        // Never reached
        printf("  [inner] ERROR: longjmp returned!\n");
        std::abort();
    }

    return_counter++;
    printf("  [inner] Returning normally (counter=%d)\n", return_counter);
}

// These frames will be skipped by longjmp - their trampolines must be restored
__attribute__((noinline))
static void skipped_frame_3(bool do_longjmp) {
    inner_capture_and_jump(do_longjmp);
    return_counter++;
    printf("  [skipped_3] Returned (counter=%d)\n", return_counter);
}

__attribute__((noinline))
static void skipped_frame_2(bool do_longjmp) {
    skipped_frame_3(do_longjmp);
    return_counter++;
    printf("  [skipped_2] Returned (counter=%d)\n", return_counter);
}

__attribute__((noinline))
static void skipped_frame_1(bool do_longjmp) {
    skipped_frame_2(do_longjmp);
    return_counter++;
    printf("  [skipped_1] Returned (counter=%d)\n", return_counter);
}

/**
 * This function has setjmp - frames above it will be skipped by longjmp,
 * but this frame and frames below it still have trampolines installed
 * and must return correctly.
 */
__attribute__((noinline))
static int setjmp_frame() {
    printf("[setjmp_frame] Setting jump point...\n");

    if (setjmp(jump_buffer) == 0) {
        // First time: descend into frames that will be skipped
        printf("[setjmp_frame] First pass - calling into skipped frames\n");
        skipped_frame_1(true);  // This will longjmp back
        // Never reached
        printf("[setjmp_frame] ERROR: should not reach here\n");
        return -1;
    } else {
        // Returned via longjmp - the skipped frames were bypassed
        // Trampolines on skipped frames should have been restored
        printf("[setjmp_frame] Returned via longjmp!\n");
        longjmp_triggered = 1;

        // Now return through THIS frame (which still has a trampoline)
        // If the shadow stack is corrupted, this will crash
        return_counter++;
        printf("[setjmp_frame] Returning normally after longjmp (counter=%d)\n", return_counter);
        return 0;
    }
}

// These frames are BELOW setjmp and must return correctly after longjmp
__attribute__((noinline))
static int outer_frame_2() {
    int result = setjmp_frame();
    return_counter++;
    printf("[outer_2] Returned from setjmp_frame (counter=%d)\n", return_counter);
    return result;
}

__attribute__((noinline))
static int outer_frame_1() {
    int result = outer_frame_2();
    return_counter++;
    printf("[outer_1] Returned from outer_2 (counter=%d)\n", return_counter);
    return result;
}

int main() {
    printf("=== GhostStack longjmp Test ===\n\n");
    printf("This test verifies that longjmp detection works correctly:\n");
    printf("- Trampolines are installed on all frames\n");
    printf("- longjmp skips some frames (their trampolines must be restored)\n");
    printf("- Remaining frames return correctly through their trampolines\n\n");

    ghost_stack_init(nullptr);

    return_counter = 0;
    int result = outer_frame_1();

    printf("\n--- Results ---\n");
    printf("longjmp triggered: %s\n", longjmp_triggered ? "YES" : "NO");
    printf("frames captured: %zu\n", frames_captured);
    printf("return counter: %d (expected: 4 = setjmp_frame + outer_2 + outer_1 + implicit)\n", return_counter);

    // Don't reset - let the remaining trampolines be used on return from main
    // This tests that the shadow stack state is correct

    bool passed = true;

    if (!longjmp_triggered) {
        printf("FAIL: longjmp was not triggered\n");
        passed = false;
    }

    if (frames_captured == 0) {
        printf("FAIL: no frames were captured\n");
        passed = false;
    }

    // We expect: setjmp_frame returns (+1), outer_2 returns (+1), outer_1 returns (+1)
    // That's 3 returns from the frames below/at setjmp point
    if (return_counter < 3) {
        printf("FAIL: not enough frames returned correctly (got %d, expected >= 3)\n", return_counter);
        passed = false;
    }

    // Clean up
    ghost_stack_reset();

    printf("\n");
    if (passed) {
        printf("=== Test PASSED ===\n");
        return 0;
    } else {
        printf("=== Test FAILED ===\n");
        return 1;
    }
}
