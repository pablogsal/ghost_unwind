/**
 * GhostStack Implementation
 * =========================
 * Shadow stack-based fast unwinding with O(1) cached captures.
 */

#include "ghost_stack.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <mutex>
#include <vector>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#ifdef __APPLE__
#include <execinfo.h>
#endif

// Assembly trampoline (defined in *_trampoline.s)
extern "C" void nwind_ret_trampoline();

// ============================================================================
// Platform Configuration
// ============================================================================

#if defined(__aarch64__) || defined(__arm64__)
    #define GS_ARCH_AARCH64 1
    #define GS_SP_REGISTER UNW_AARCH64_X29
    #define GS_RA_REGISTER UNW_AARCH64_X30
#elif defined(__x86_64__)
    #define GS_ARCH_X86_64 1
    #define GS_SP_REGISTER UNW_X86_64_RBP
    #define GS_RA_REGISTER UNW_X86_64_RIP
#else
    #error "Unsupported architecture"
#endif

#ifndef GHOST_STACK_MAX_FRAMES
#define GHOST_STACK_MAX_FRAMES 512
#endif

// ============================================================================
// Logging (minimal, stderr only)
// ============================================================================

#ifdef DEBUG
#define LOG_DEBUG(...) fprintf(stderr, "[GhostStack] " __VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#endif

#define LOG_ERROR(...) fprintf(stderr, "[GhostStack][ERROR] " __VA_ARGS__)

// ============================================================================
// Utilities
// ============================================================================

#ifdef GS_ARCH_AARCH64
static inline uintptr_t ptrauth_strip(uintptr_t val) {
    uint64_t ret;
    asm volatile(
        "mov x30, %1\n\t"
        "xpaclri\n\t"
        "mov %0, x30\n\t"
        : "=r"(ret) : "r"(val) : "x30");
    return ret;
}
#else
static inline uintptr_t ptrauth_strip(uintptr_t val) { return val; }
#endif

// ============================================================================
// Stack Entry
// ============================================================================

struct StackEntry {
    uintptr_t return_address;   // Original return address
    uintptr_t* location;        // Where it lives on the stack
    uintptr_t stack_pointer;    // SP at capture time (for validation)
};

// ============================================================================
// GhostStack Core (thread-local)
// ============================================================================

class GhostStackImpl {
public:
    GhostStackImpl() {
        entries_.reserve(64);
    }

    ~GhostStackImpl() {
        reset();
    }

    // Set custom unwinder (NULL = use default libunwind)
    void set_unwinder(ghost_stack_unwinder_t unwinder) {
        custom_unwinder_ = unwinder;
    }

    // Main capture function - returns number of frames
    size_t backtrace(void** buffer, size_t max_frames) {
        if (is_capturing_) {
            return 0;  // Recursive call, bail out
        }
        is_capturing_ = true;

        size_t result = 0;

        // Fast path: trampolines installed, return cached frames
        if (trampolines_installed_ && !entries_.empty()) {
            result = copy_cached_frames(buffer, max_frames);
            is_capturing_ = false;
            return result;
        }

        // Slow path: capture with unwinder and install trampolines
        result = capture_and_install(buffer, max_frames);
        is_capturing_ = false;
        return result;
    }

    // Reset: restore original return addresses
    void reset() {
        if (trampolines_installed_) {
            for (size_t i = location_; i < entries_.size(); ++i) {
                *entries_[i].location = entries_[i].return_address;
            }
        }
        entries_.clear();
        location_ = 0;
        trampolines_installed_ = false;
    }

    // Called by trampoline when a function returns
    uintptr_t on_ret_trampoline(uintptr_t sp) {
        if (entries_.empty() || location_ >= entries_.size()) {
            LOG_ERROR("Stack corruption in trampoline!\n");
            std::abort();
        }

        auto& entry = entries_[location_++];

        // Sanity check (warning only, doesn't abort)
        if (sp != 0 && entry.stack_pointer != 0 && entry.stack_pointer != sp) {
            LOG_DEBUG("SP mismatch: expected 0x%lx, got 0x%lx\n",
                      entry.stack_pointer, sp);
        }

        return entry.return_address;
    }

private:
    // Copy cached frames to output buffer
    size_t copy_cached_frames(void** buffer, size_t max_frames) {
        size_t available = entries_.size() - location_;
        size_t count = (available < max_frames) ? available : max_frames;

        for (size_t i = 0; i < count; ++i) {
            buffer[i] = reinterpret_cast<void*>(entries_[location_ + i].return_address);
        }

        LOG_DEBUG("Fast path: %zu frames\n", count);
        return count;
    }

    // Capture frames using unwinder, install trampolines
    size_t capture_and_install(void** buffer, size_t max_frames) {
        // First, capture IPs using the unwinder
        std::vector<void*> raw_frames(max_frames);
        size_t raw_count = do_unwind(raw_frames.data(), max_frames);

        if (raw_count == 0) {
            return 0;
        }

        // Now walk the stack to get return address locations and install trampolines
        std::vector<StackEntry> new_entries;
        new_entries.reserve(raw_count);
        bool found_existing = false;

        unw_context_t ctx;
        unw_cursor_t cursor;
        unw_getcontext(&ctx);
        unw_init_local(&cursor, &ctx);

        // Skip internal frames (platform-specific due to backtrace/libunwind differences)
#ifdef __APPLE__
        // macOS: Skip fewer frames due to backtrace()/libunwind difference
        for (int i = 0; i < 1 && unw_step(&cursor) > 0; ++i) {}
#else
        // Linux: Skip internal frames (this function + backtrace)
        for (int i = 0; i < 3 && unw_step(&cursor) > 0; ++i) {}
#endif

        size_t frame_idx = 0;
        while (unw_step(&cursor) > 0 && frame_idx < raw_count) {
            unw_word_t ip, sp;
            unw_get_reg(&cursor, UNW_REG_IP, &ip);
            unw_get_reg(&cursor, GS_SP_REGISTER, &sp);

            // Get location where return address is stored
            uintptr_t* ret_loc = nullptr;
#ifdef __linux__
            unw_save_loc_t loc;
            if (unw_get_save_loc(&cursor, GS_RA_REGISTER, &loc) == 0 &&
                loc.type == UNW_SLT_MEMORY) {
                ret_loc = reinterpret_cast<uintptr_t*>(loc.u.addr);
            }
#else
            // macOS: return address is at fp + sizeof(void*)
            ret_loc = reinterpret_cast<uintptr_t*>(sp + sizeof(void*));
#endif
            if (!ret_loc) break;

            uintptr_t ret_addr = *ret_loc;

            // Check if already patched (cache hit)
            if (ret_addr == reinterpret_cast<uintptr_t>(nwind_ret_trampoline)) {
                found_existing = true;
                LOG_DEBUG("Found existing trampoline at frame %zu\n", frame_idx);
                break;
            }

            new_entries.push_back({ret_addr, ret_loc, sp});
            frame_idx++;
        }

        // Install trampolines on new entries
        for (auto& e : new_entries) {
            *e.location = reinterpret_cast<uintptr_t>(nwind_ret_trampoline);
        }

        // Merge with existing entries if we found a patched frame
        if (found_existing && !entries_.empty()) {
            new_entries.insert(new_entries.end(),
                               entries_.begin() + static_cast<long>(location_),
                               entries_.end());
        }

        entries_ = std::move(new_entries);
        location_ = 0;
        trampolines_installed_ = true;

        // Copy to output buffer
        size_t count = (entries_.size() < max_frames) ? entries_.size() : max_frames;
        for (size_t i = 0; i < count; ++i) {
            buffer[i] = reinterpret_cast<void*>(entries_[i].return_address);
        }

        LOG_DEBUG("Captured %zu frames\n", count);
        return count;
    }

    // Call the unwinder (custom or default)
    size_t do_unwind(void** buffer, size_t max_frames) {
        if (custom_unwinder_) {
            return custom_unwinder_(buffer, max_frames);
        }

#ifdef __APPLE__
        // macOS: use standard backtrace function
        int ret = ::backtrace(buffer, static_cast<int>(max_frames));
        return (ret > 0) ? static_cast<size_t>(ret) : 0;
#else
        // Linux: use libunwind's unw_backtrace
        int ret = unw_backtrace(buffer, static_cast<int>(max_frames));
        return (ret > 0) ? static_cast<size_t>(ret) : 0;
#endif
    }

    std::vector<StackEntry> entries_;
    size_t location_ = 0;
    bool is_capturing_ = false;
    bool trampolines_installed_ = false;
    ghost_stack_unwinder_t custom_unwinder_ = nullptr;
};

// ============================================================================
// Thread-Local Instance
// ============================================================================

static thread_local GhostStackImpl* t_instance = nullptr;

static GhostStackImpl& get_instance() {
    if (!t_instance) {
        t_instance = new GhostStackImpl();
    }
    return *t_instance;
}

// ============================================================================
// Global State
// ============================================================================

static std::once_flag g_init_flag;
static ghost_stack_unwinder_t g_custom_unwinder = nullptr;

// ============================================================================
// C API Implementation
// ============================================================================

extern "C" {

void ghost_stack_init(ghost_stack_unwinder_t unwinder) {
    std::call_once(g_init_flag, [unwinder]() {
        g_custom_unwinder = unwinder;
        LOG_DEBUG("Initialized with %s unwinder\n",
                  unwinder ? "custom" : "default");
    });
}

size_t ghost_stack_backtrace(void** buffer, size_t size) {
    // Auto-init if needed
    std::call_once(g_init_flag, []() {
        g_custom_unwinder = nullptr;
    });

    auto& impl = get_instance();

    // Apply global unwinder setting if not already set
    static thread_local bool unwinder_set = false;
    if (!unwinder_set) {
        impl.set_unwinder(g_custom_unwinder);
        unwinder_set = true;
    }

    return impl.backtrace(buffer, size);
}

void ghost_stack_reset(void) {
    if (t_instance) {
        t_instance->reset();
    }
}

void ghost_stack_thread_cleanup(void) {
    if (t_instance) {
        delete t_instance;
        t_instance = nullptr;
    }
}

// Called by assembly trampoline
uintptr_t nwind_on_ret_trampoline(uintptr_t sp) {
    return get_instance().on_ret_trampoline(sp);
}

// Called when exception passes through trampoline
uintptr_t nwind_on_exception_through_trampoline(void* exception) {
    LOG_DEBUG("Exception through trampoline\n");

    uintptr_t ret = get_instance().on_ret_trampoline(0);
    get_instance().reset();

    __cxxabiv1::__cxa_begin_catch(exception);
    return ret;
}

} // extern "C"
