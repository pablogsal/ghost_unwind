#include "ghost_stack.hpp"
#include <cstring>
#include <cxxabi.h>
#include <dlfcn.h>
#include <iomanip>
#include <iostream>
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <sstream>
#include <sys/mman.h>
#include <execinfo.h>

extern "C" {
extern void nwind_ret_trampoline();

uintptr_t nwind_on_ret_trampoline(uintptr_t stack_pointer) {
  return GhostStack::get().on_ret_trampoline(stack_pointer);
}

uintptr_t nwind_on_exception_through_trampoline(void *exception) {
  printf("Oh no!\n");
  uintptr_t return_addr = GhostStack::get().on_ret_trampoline(0);
  GhostStack::get().reset();
  __cxxabiv1::__cxa_begin_catch(exception);
  return return_addr;
}
}

thread_local std::unique_ptr<GhostStack> GhostStack::instance;

GhostStack &GhostStack::get() {
  if (!instance) {
    instance = std::unique_ptr<GhostStack>(new GhostStack());
  }
  return *instance;
}

// Helper function to demangle C++ names
static std::string demangle(const char *symbol) {
  int status;
  char *demangled = abi::__cxa_demangle(symbol, nullptr, nullptr, &status);
  if (status == 0 && demangled) {
    std::string result(demangled);
    free(demangled);
    return result;
  }
  return symbol;
}

// Helper function to symbolize an address
std::string symbolize_address(unw_word_t addr) {
  unw_context_t context;
  unw_cursor_t cursor;
  char sym[256];
  unw_word_t offset;
  std::ostringstream result;

  // Create a new context and cursor for the process
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);

  // Set the IP in the cursor to our target address
  unw_set_reg(&cursor, UNW_REG_IP, addr);

  // Now get the proc name for this IP
  if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0) {
    result << std::hex << "0x" << addr << " <" << demangle(sym) << "+0x"
           << offset << ">";
  } else {
    result << std::hex << "0x" << addr << " <unknown>";
  }

  return result.str();
}

uintptr_t GhostStack::on_ret_trampoline(uintptr_t stack_pointer) {
  if (entries.empty()) {
    std::cerr << "Ghost stack underflow!" << std::endl;
    std::abort();
  }

  if (location >= entries.size()) {
    std::cerr << "Ghost stack overflow!" << std::endl;
    std::cerr << location << " > " << entries.size() << std::endl;
    std::abort();
  }

  auto &entry = entries[location++];
  if (entry.stack_pointer != stack_pointer && stack_pointer != 0) {
    std::cerr << "Stack pointer mismatch! Expected: " << std::hex
              << entry.stack_pointer << " Got: " << stack_pointer << std::endl;
    std::cerr << "Stack pointer diff:" << stack_pointer - entry.stack_pointer
              << std::endl;
    // std::abort();
  }
  if (entry.return_address == (uintptr_t)nwind_ret_trampoline) {
    std::cerr << "Already patched frame!" << std::endl;
    std::abort();
  }
  auto ret_addr = entry.return_address;

  // Print symbolized return address
  std::cout << "Returning to: " << symbolize_address(ret_addr) << std::endl;

  return ret_addr;
}

#include <cstdint>

#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
static inline uint64_t ptrauth_strip(uint64_t __value, unsigned int __key) {
  // On the stack the link register is protected with Pointer
  // Authentication Code when compiled with -mbranch-protection.
  uint64_t ret;
  asm volatile(
      "mov x30, %1\n\t"
      // "hint #7\n\t"  // xpaclri
      "xpaclri\n\t"
      "mov %0, x30\n\t"
      : "=r"(ret)
      : "r"(__value)
      : "x30");
  return ret;
}
#else
static inline uint64_t ptrauth_strip(uint64_t __value, unsigned int __key) {
  return __value;
}
#endif

#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
#define SP_REGISTER UNW_AARCH64_X29
#define RA_REGISTER UNW_AARCH64_X30
#elif defined(__x86_64__)
#define SP_REGISTER UNW_X86_64_RBP
#define RA_REGISTER UNW_X86_64_RIP
#else
#error "Unsupported architecture"
#endif

__attribute__((noinline)) 
void GhostStack::capture_stack_trace(bool install_trampolines) {
  std::vector<StackEntry> new_entries;
  bool found_existing_frame = false;

  // Initialize unwinding
  unw_context_t context;
  unw_cursor_t cursor;
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);

  // Skip first frame (capture_stack_trace)
  unw_step(&cursor);
  unw_step(&cursor);
  unw_word_t ip, fp;
  unw_get_reg(&cursor, UNW_REG_IP, &ip);
  unw_get_reg(&cursor, SP_REGISTER, &fp);

  while (unw_step(&cursor)) {

#ifdef __linux__
    // Get save location for current frame
    unw_save_loc_t saveLoc;
    unw_get_save_loc(&cursor, RA_REGISTER, &saveLoc);
    if (saveLoc.type != UNW_SLT_MEMORY) {
      std::cout << "Warning: Return address not stored in memory at " 
                << symbolize_address(ip) << std::endl;
      break;
    }
    uintptr_t *ret_addr_loc = (uintptr_t*)saveLoc.u.addr;
#else
    uintptr_t *ret_addr_loc = (uintptr_t*)(fp + sizeof(void*));
#endif

    // Now saveLoc points to the return address location for the previous frame
    printf("Return addr loc is: %p\n", ret_addr_loc);
    uintptr_t ret_addr = *ret_addr_loc;
    std::cout << "Return addr is: " << symbolize_address(ret_addr) << std::endl;

    // Check for existing trampoline
    if (ret_addr == (uintptr_t)nwind_ret_trampoline) {
      found_existing_frame = true;
      std::cout << "Found already patched frame, stopping capture\n";
      break;
    }

    // Make the page containing the return address writable
    uintptr_t page_start = (uintptr_t)ret_addr_loc & ~(0xFFF);
    mprotect((void *)page_start, 0x1000, PROT_READ | PROT_WRITE);

    ip = ptrauth_strip(ip, 0);

    new_entries.push_back({ret_addr, ret_addr_loc, 0, (uintptr_t)ip});

    // Get current frame's IP for next iteration
    unw_get_reg(&cursor, UNW_REG_IP, &ip);
    unw_get_reg(&cursor, SP_REGISTER, &fp);
  }

  std::cerr << "Using 100% of " << new_entries.size() << " frames" << std::endl;

  // Install trampolines for new entries
  if (install_trampolines && new_entries.size()) {
    // Validate that return addresses match next frame's IP
    for (size_t i = 0; i < new_entries.size() - 1; i++) {
      if (new_entries[i].return_address != new_entries[i + 1].ip) {
        std::cerr << "Stack frame validation failed at frame " << i << "!\n"
                  << "Return address: " << symbolize_address(new_entries[i].return_address) << "\n"
                  << "Next frame IP: " << symbolize_address(new_entries[i + 1].ip) << std::endl;
        return;
      }
    }
    for (const auto &entry : new_entries) {
      *entry.location = (uintptr_t)nwind_ret_trampoline;
    }
  }

  // Handle merging if we found existing frame
  if (found_existing_frame && !entries.empty()) {
    size_t total = entries.size() + new_entries.size();
    std::cerr << "Using " << (entries.size() * 100.0f / total)
              << "% of existing frames" << std::endl;

    new_entries.insert(new_entries.end(), entries.begin() + location,
                      entries.end());
  }



  entries = std::move(new_entries);
  location = 0;
}
// New function to get current stack trace using ghost stack
__attribute__((noinline)) 
const std::vector<uintptr_t> GhostStack::unwind(bool install_trampolines) {
  // First ensure all frames are patched
  capture_stack_trace(install_trampolines);

  // Create vector of return addresses in correct order
  std::vector<uintptr_t> stack_trace;
  for (const auto &entry : entries) {
  std::cout << "STACK : " << symbolize_address(entry.return_address) << std::endl;
    stack_trace.push_back(entry.return_address);
  }

  return stack_trace;
}

void GhostStack::reset() {
  // Restore all original return addresses
  for (size_t i = location; i < entries.size(); i++) {
    auto &entry = entries[i];
    *(entry.location) = entry.return_address;
  }
  entries.clear();
}
