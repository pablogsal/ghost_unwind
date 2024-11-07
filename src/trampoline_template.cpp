#include <cxxabi.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uintptr_t nwind_on_ret_trampoline(uintptr_t stack_pointer);

// The simplest possible function that will force the compiler to generate
// just register saves and the call
__attribute__((noinline, used, cold)) void nwind_ret_trampoline(void) {
  try {
    uintptr_t ret;
    ret = nwind_on_ret_trampoline(
        0); // The 0 will be replaced with actual sp in asm
    ((void (*)(void))ret)();
  } catch (...) {
    nwind_on_ret_trampoline(0);
    __cxxabiv1::__cxa_rethrow();
  }
}

#ifdef __cplusplus
}
#endif
