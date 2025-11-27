/**
 * GhostStack fork() Test
 *
 * Tests that the shadow stack correctly handles fork() by clearing
 * the shadow stack in the child process via pthread_atfork handler.
 *
 * Test strategy:
 * 1. Capture stack trace in parent (installs trampolines on current frames)
 * 2. Fork from within the call stack that has trampolines installed
 * 3. Child returns through those frames - if shadow stack wasn't cleared,
 *    the trampoline would use stale/inherited shadow stack state
 * 4. Parent also returns through its frames normally
 *
 * Key insight: The fork happens INSIDE the functions that have trampolines,
 * so both parent and child must return through trampolined frames after fork.
 */

#include "ghost_stack.h"
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

static volatile int child_return_count = 0;
static volatile int parent_return_count = 0;

/**
 * This function is called deep in the stack, captures, then forks.
 * Both parent and child must return through the trampolined frames above.
 */
__attribute__((noinline))
static pid_t capture_and_fork() {
    void* frames[64];
    size_t count = ghost_stack_backtrace(frames, 64);
    printf("[pid=%d] Captured %zu frames, about to fork...\n", getpid(), count);
    fflush(stdout);

    if (count == 0) {
        printf("[pid=%d] ERROR: No frames captured\n", getpid());
        return -1;
    }

    pid_t pid = fork();
    // Both parent and child continue from here and return through trampolined frames
    return pid;
}

// These frames have trampolines installed when we fork
// Both parent and child must return through them correctly
__attribute__((noinline))
static pid_t depth_4() {
    pid_t pid = capture_and_fork();
    if (pid == 0) {
        child_return_count++;
        printf("[child] depth_4 returning (count=%d)\n", child_return_count);
    } else if (pid > 0) {
        parent_return_count++;
        printf("[parent] depth_4 returning (count=%d)\n", parent_return_count);
    }
    fflush(stdout);
    return pid;
}

__attribute__((noinline))
static pid_t depth_3() {
    pid_t pid = depth_4();
    if (pid == 0) {
        child_return_count++;
        printf("[child] depth_3 returning (count=%d)\n", child_return_count);
    } else if (pid > 0) {
        parent_return_count++;
        printf("[parent] depth_3 returning (count=%d)\n", parent_return_count);
    }
    fflush(stdout);
    return pid;
}

__attribute__((noinline))
static pid_t depth_2() {
    pid_t pid = depth_3();
    if (pid == 0) {
        child_return_count++;
        printf("[child] depth_2 returning (count=%d)\n", child_return_count);
    } else if (pid > 0) {
        parent_return_count++;
        printf("[parent] depth_2 returning (count=%d)\n", parent_return_count);
    }
    fflush(stdout);
    return pid;
}

__attribute__((noinline))
static pid_t depth_1() {
    pid_t pid = depth_2();
    if (pid == 0) {
        child_return_count++;
        printf("[child] depth_1 returning (count=%d)\n", child_return_count);
    } else if (pid > 0) {
        parent_return_count++;
        printf("[parent] depth_1 returning (count=%d)\n", parent_return_count);
    }
    fflush(stdout);
    return pid;
}

int main() {
    printf("=== GhostStack fork() Test ===\n\n");
    printf("This test verifies fork safety:\n");
    printf("- Trampolines are installed on stack frames\n");
    printf("- Fork happens inside those frames\n");
    printf("- Both parent and child return through trampolined frames\n");
    printf("- If fork handler didn't clear child's shadow stack, child would crash\n\n");

    ghost_stack_init(nullptr);

    pid_t pid = depth_1();

    if (pid < 0) {
        printf("=== Test FAILED (fork error) ===\n");
        return 1;
    }

    if (pid == 0) {
        // Child process - we've successfully returned through all trampolined frames
        printf("\n[child] Successfully returned through %d trampolined frames\n", child_return_count);
        fflush(stdout);

        if (child_return_count < 4) {
            printf("[child] FAIL: Expected to return through 4 frames, got %d\n", child_return_count);
            fflush(stdout);
            _exit(1);
        }

        // Capture again to verify shadow stack is in clean state
        void* frames[64];
        size_t count = ghost_stack_backtrace(frames, 64);
        printf("[child] After fork, captured %zu frames (fresh capture)\n", count);
        fflush(stdout);

        ghost_stack_reset();
        printf("[child] Test PASSED\n");
        fflush(stdout);
        _exit(0);
    } else {
        // Parent process - wait for child and verify parent also worked
        printf("\n[parent] Successfully returned through %d trampolined frames\n", parent_return_count);
        printf("[parent] Waiting for child (pid=%d)...\n", pid);

        int status;
        waitpid(pid, &status, 0);

        bool passed = true;

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("[parent] FAIL: Child exited abnormally (status=%d)\n", status);
            passed = false;
        } else {
            printf("[parent] Child exited successfully\n");
        }

        if (parent_return_count < 4) {
            printf("[parent] FAIL: Expected to return through 4 frames, got %d\n", parent_return_count);
            passed = false;
        }

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
}
