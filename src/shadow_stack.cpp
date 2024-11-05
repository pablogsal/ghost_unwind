#include "shadow_stack.hpp"
#include <cstring>
#include <cxxabi.h>
#include <dlfcn.h>
#include <iomanip>
#include <iostream>
#include <libunwind.h>
#include <sstream>
#include <sys/mman.h>

extern "C" {
extern void nwind_ret_trampoline();

uintptr_t nwind_on_ret_trampoline(uintptr_t stack_pointer) {
  return ShadowStack::get().on_ret_trampoline(stack_pointer);
}

uintptr_t nwind_on_exception_through_trampoline(void *exception) {
  printf("Oh no!\n");
  uintptr_t return_addr = ShadowStack::get().on_ret_trampoline(0);
  ShadowStack::get().reset();
  __cxa_begin_catch(exception);
  return return_addr;
}
}

thread_local std::unique_ptr<ShadowStack> ShadowStack::instance;

ShadowStack &ShadowStack::get() {
  if (!instance) {
    instance = std::unique_ptr<ShadowStack>(new ShadowStack());
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

uintptr_t ShadowStack::on_ret_trampoline(uintptr_t stack_pointer) {
  if (entries.empty()) {
    std::cerr << "Shadow stack underflow!" << std::endl;
    std::abort();
  }

  if (location >= entries.size()) {
    std::cerr << "Shadow stack overflow!" << std::endl;
    std::cerr << location << " > " << entries.size() << std::endl;
    std::abort();
  }

  auto &entry = entries[location++];
  if (entry.stack_pointer != stack_pointer && stack_pointer != 0) {
    std::cerr << "Stack pointer mismatch! Expected: " << std::hex
              << entry.stack_pointer << " Got: " << stack_pointer << std::endl;
    std::cerr << "Stack pointer diff:" << stack_pointer - entry.stack_pointer
              << std::endl;
    std::abort();
  }
  if (entry.return_address == (uintptr_t)nwind_ret_trampoline) {
    std::cerr << "Already patched frame!" << std::endl;
    std::abort();
  }
  // std::cerr << "Returning to:" << (void *)entry.return_address << std::endl;

  auto ret_addr = entry.return_address;

  // Print symbolized return address
  // std::cout << "Returning to: " << symbolize_address(ret_addr) << std::endl;

  return ret_addr;
}

void ShadowStack::capture_stack_trace(bool install_trampolines) {
  std::vector<StackEntry> new_entries;
  bool found_existing_frame = false;

  // Initialize unwinding
  unw_context_t context;
  unw_cursor_t cursor;
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);

  // Skip first two frames (capture_stack_trace and its caller)
  unw_step(&cursor); // Skip our frame
  unw_step(&cursor); // Skip caller's frame

  uintptr_t *ret_addr_loc = nullptr;

  while (unw_step(&cursor) > 0) {
    unw_word_t ip, bp, sp;
    unw_get_reg(&cursor, UNW_REG_IP, &ip);
    unw_get_reg(&cursor, UNW_X86_64_RBP, &bp);
    unw_get_reg(&cursor, UNW_REG_SP, &sp);

    // Now sp-8 points to the return address location we actually want to patch
    ret_addr_loc = (uintptr_t *)(sp - sizeof(void *));
    if (!ret_addr_loc) {
      continue;
    }

    uintptr_t ret_addr = *ret_addr_loc;

    // Check for existing trampoline
    if (ret_addr == (uintptr_t)nwind_ret_trampoline) {
      found_existing_frame = true;
      std::cout << "Found already patched frame, stopping capture\n";
      break;
    }

    // Make the page containing the return address writable
    uintptr_t page_start = (uintptr_t)ret_addr_loc & ~(0xFFF);
    mprotect((void *)page_start, 0x1000, PROT_READ | PROT_WRITE);

    // Store the entry
    new_entries.push_back(
        {ret_addr, ret_addr_loc, (uintptr_t)ret_addr_loc + 8});
  }

  // Handle merging if we found existing frame
  if (found_existing_frame && !entries.empty()) {
    size_t total = entries.size() + new_entries.size();
    std::cerr << "Using " << (entries.size() * 100.0f / total)
              << "% of existing frames" << std::endl;

    new_entries.insert(new_entries.end(), entries.begin() + location,
                       entries.end());
  }

  std::cerr << "Using 100% of " << new_entries.size() << " frames" << std::endl;

  // Install trampolines for new entries
  if (install_trampolines) {
    for (const auto &entry : new_entries) {
      *entry.location = (uintptr_t)nwind_ret_trampoline;
    }
  }

  entries = std::move(new_entries);
  location = 0;
}
// New function to get current stack trace using shadow stack
const std::vector<uintptr_t> ShadowStack::unwind(bool install_trampolines) {
  // First ensure all frames are patched
  capture_stack_trace(install_trampolines);

  // Create vector of return addresses in correct order
  std::vector<uintptr_t> stack_trace;
  for (const auto &entry : entries) {
    stack_trace.push_back(entry.return_address);
  }

  return stack_trace;
}

void ShadowStack::reset() {
  // Restore all original return addresses
  for (size_t i = location; i < entries.size(); i++) {
    auto &entry = entries[i];
    *(entry.location) = entry.return_address;
  }
  entries.clear();
}
