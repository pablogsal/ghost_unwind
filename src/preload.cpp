#define _GNU_SOURCE
#include "shadow_stack.hpp"
#include <ctime>
#include <dlfcn.h>
#include <iomanip>
#include <iostream>
#include <pthread.h>
#include <unistd.h>

thread_local bool in_trampoline = false;

// Type for real read function
typedef ssize_t (*real_read_t)(int fd, void *buf, size_t count);

// Global to store real function pointer
static real_read_t real_read = nullptr;

// Mutex for thread-safe logging
static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize when library is loaded
__attribute__((constructor)) static void init() {
  // Get the real read function
  real_read = (real_read_t)dlsym(RTLD_NEXT, "read");
  if (!real_read) {
    std::cerr << "Failed to get real read function: " << dlerror() << std::endl;
    abort();
  }
}

// Our intercepted read function
extern "C" ssize_t read(int fd, void *buf, size_t count) {
  if (in_trampoline) {
    return real_read(fd, buf, count);
  }
  in_trampoline = true;
  // Get stack trace before calling read
  auto trace = ShadowStack::get().unwind(true);

  // Call real read
  ssize_t result = real_read(fd, buf, count);

  // Log the call with stack trace
  pthread_mutex_lock(&print_mutex);
  auto now = std::time(nullptr);
  std::cout << "=== read() call at " << std::ctime(&now);
  // std::cout << "fd: " << fd << ", count: " << count << ", result: " << result
  // << std::endl; std::cout << "Stack trace:" << std::endl; std::cout << "-->"
  // << trace.size() << std::endl; for (size_t i = 0; i < trace.size(); i++) {
  //     std::cout << "#" << i << " " << std::hex << "0x" << trace[i] <<
  //     std::dec << std::endl;
  // }
  // std::cout << "===" << std::endl;
  pthread_mutex_unlock(&print_mutex);
  in_trampoline = false;

  return result;
}
