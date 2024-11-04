#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#define UNW_LOCAL_ONLY
#include <libunwind.h>

struct StackEntry {
    uintptr_t return_address;  // Original return address
    uintptr_t* location;       // Location of return address on stack
    uintptr_t stack_pointer;   // Stack pointer value
};

class ShadowStack {
public:
    static ShadowStack& get();
    uintptr_t on_ret_trampoline(uintptr_t stack_pointer);
    void capture_stack_trace();
    const std::vector<uintptr_t> unwind();
    void reset();

private:
    ShadowStack() = default;
    std::vector<StackEntry> entries;
    static thread_local std::unique_ptr<ShadowStack> instance;
};