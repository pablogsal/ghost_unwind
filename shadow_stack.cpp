#include "shadow_stack.hpp"
#include <iostream>
#include <cstring>
#include <sys/mman.h>
#include <libunwind.h>
#include <cxxabi.h>
#include <sstream>
#include <iomanip>
#include <dlfcn.h>

extern "C" {
    extern void nwind_ret_trampoline();
    
    uintptr_t nwind_on_ret_trampoline(uintptr_t stack_pointer) {
        return ShadowStack::get().on_ret_trampoline(stack_pointer);
    }
}

thread_local std::unique_ptr<ShadowStack> ShadowStack::instance;

ShadowStack& ShadowStack::get() {
    if (!instance) {
        instance = std::unique_ptr<ShadowStack>(new ShadowStack());
    }
    return *instance;
}

// Helper function to demangle C++ names
static std::string demangle(const char* symbol) {
    int status;
    char* demangled = abi::__cxa_demangle(symbol, nullptr, nullptr, &status);
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
        result << std::hex << "0x" << addr 
               << " <" << demangle(sym) 
               << "+0x" << offset << ">";
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

    auto& entry = entries.front();  // Use front instead of back
    if (entry.stack_pointer != stack_pointer) {
        std::cerr << "Stack pointer mismatch! Expected: " << std::hex 
                  << entry.stack_pointer << " Got: " << stack_pointer << std::endl;
        std::cerr << "Stack pointer diff:" << stack_pointer - entry.stack_pointer << std::endl;
        // std::abort();
    }

    // Restore original return address
    auto ret_addr = entry.return_address;
    *(entry.location) = ret_addr;

      // Print symbolized return address
    std::cout << "Returning to: " << symbolize_address(ret_addr) << std::endl;

    entries.erase(entries.begin());  // Remove from front instead of pop_back
    return ret_addr;
}

void ShadowStack::capture_stack_trace() {
    unw_context_t context;
    unw_cursor_t cursor;
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    // Skip first frame (capture_stack_trace)
    unw_step(&cursor);
    
    while (unw_step(&cursor) > 0) {
        unw_word_t ip, bp;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        unw_get_reg(&cursor, UNW_X86_64_RBP, &bp);

        if (bp == 0) {
            break;
        }
        
        // Return address is at bp+8
        uintptr_t* ret_addr_loc = (uintptr_t*)(bp + 8);
        uintptr_t ret_addr = *ret_addr_loc;

        // If we find a trampoline, we're done
        if (ret_addr == (uintptr_t)nwind_ret_trampoline) {
            std::cout << "Found already patched frame, stopping capture\n";
            break;
        }

        std::cout << "Capturing frame: " << symbolize_address(ret_addr) << std::endl;
        
        // Make the page containing the return address writable
        uintptr_t page_start = (uintptr_t)ret_addr_loc & ~(0xFFF);
        mprotect((void*)page_start, 0x1000, PROT_READ | PROT_WRITE);
        
        // Store entry and inject trampoline
        entries.push_back({
            ret_addr,
            ret_addr_loc,
            (uintptr_t)ret_addr_loc + 8
        });
        
        *ret_addr_loc = (uintptr_t)nwind_ret_trampoline;
    }
}



// New function to get current stack trace using shadow stack
const std::vector<uintptr_t> ShadowStack::unwind() {
    // First ensure all frames are patched
    capture_stack_trace();
    
    // Create vector of return addresses in correct order
    std::vector<uintptr_t> stack_trace;
    for (const auto& entry : entries) {
        stack_trace.push_back(entry.return_address);
    }
    
    // Print the trace
    std::cout << "\nStack trace:\n";
    for (size_t i = 0; i < entries.size(); i++) {
        std::cout << "#" << i << " " << symbolize_address(stack_trace[i]) << std::endl;
    }
    
    return stack_trace;
}

void ShadowStack::reset() {
    // Restore all original return addresses
    for (auto& entry : entries) {
        *(entry.location) = entry.return_address;
    }
    entries.clear();
}