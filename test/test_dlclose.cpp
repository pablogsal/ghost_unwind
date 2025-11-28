/**
 * GhostStack dlclose Safety Test
 *
 * Tests that when a shared library containing ghost_stack is unloaded via
 * dlclose(), all trampolines are properly restored to their original return
 * addresses via the InstanceRegistry destructor.
 *
 * CRITICAL: We do NOT call cleanup() before dlclose(). The whole point is
 * to test that InstanceRegistry properly restores trampolines on library unload.
 */

#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

typedef void (*init_fn)(void);
typedef size_t (*capture_fn)(void**, size_t);

static volatile int execution_counter = 0;

__attribute__((noinline))
static void post_dlclose_function() {
    execution_counter++;
    printf("[test] post_dlclose_function executed (counter=%d)\n", execution_counter);
}

__attribute__((noinline))
static size_t wrapper_depth_2(capture_fn capture, void** frames) {
    return capture(frames, 64);
}

__attribute__((noinline))
static size_t wrapper_depth_1(capture_fn capture, void** frames) {
    return wrapper_depth_2(capture, frames);
}

int main() {
    printf("=== GhostStack dlclose Safety Test ===\n\n");

#ifdef __APPLE__
    void* handle = dlopen("./libdlclose_helper.dylib", RTLD_NOW);
#else
    void* handle = dlopen("./libdlclose_helper.so", RTLD_NOW);
#endif
    if (!handle) {
        printf("[test] FAILED: Could not open library: %s\n", dlerror());
        return 1;
    }

    auto init = reinterpret_cast<init_fn>(dlsym(handle, "dlclose_helper_init"));
    auto capture = reinterpret_cast<capture_fn>(dlsym(handle, "dlclose_helper_capture"));

    if (!init || !capture) {
        printf("[test] FAILED: Could not find symbols\n");
        dlclose(handle);
        return 1;
    }

    init();

    void* frames[64];
    size_t count = wrapper_depth_1(capture, frames);
    printf("[test] Captured %zu frames\n", count);

    if (count == 0) {
        printf("[test] FAILED: No frames captured\n");
        dlclose(handle);
        return 1;
    }

    // NOTE: Intentionally NOT calling cleanup() before dlclose
    // InstanceRegistry destructor must restore trampolines
    printf("[test] Closing library WITHOUT cleanup (testing InstanceRegistry)...\n");

    int ret = dlclose(handle);
    if (ret != 0) {
        printf("[test] FAILED: dlclose returned %d\n", ret);
        return 1;
    }

    // If trampolines weren't restored, we'd crash here or on return
    post_dlclose_function();
    post_dlclose_function();

    if (execution_counter != 2) {
        printf("[test] FAILED: execution_counter=%d, expected 2\n", execution_counter);
        return 1;
    }

    printf("\n=== Test PASSED ===\n");
    return 0;
}
