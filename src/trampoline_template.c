#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uintptr_t nwind_on_ret_trampoline(uintptr_t stack_pointer);

// The simplest possible function that will force the compiler to generate 
// just register saves and the call
__attribute__((naked, noinline, used, cold))
void nwind_ret_trampoline(void) {
    volatile register uintptr_t ret;
    ret = nwind_on_ret_trampoline(0);  // The 0 will be replaced with actual sp in asm
    ((void(*)(void))ret)();
}

#ifdef __cplusplus
}
#endif