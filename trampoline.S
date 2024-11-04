.intel_syntax noprefix
.text
.global nwind_ret_trampoline
.type nwind_ret_trampoline, @function

# This is the trampoline function that gets injected into return addresses.
# It preserves all necessary registers, calls our handler, and then jumps
# to the original return address.
nwind_ret_trampoline:
    # First, we need to preserve any registers that might contain return values
    # or that are caller-saved. RAX holds return values, RDX might contain
    # the high bits of a 128-bit return value, and RCX might be used for
    # variable argument functions.
    push rax
    push rdx
    push rcx

    # The AMD64 ABI requires the stack to be aligned on a 16-byte boundary
    # before making a call. We've pushed 3 registers (24 bytes), so we need
    # to subtract 8 more bytes to reach proper alignment.
    sub rsp, 8

    # Set up the argument for our handler function. The handler needs the original
    # stack pointer (before our pushes) to find the right shadow stack entry.
    # RDI is the first argument register in the AMD64 ABI.
    mov rdi, rsp
    add rdi, 32     # Add back the size of our pushes (24 bytes) plus alignment (8 bytes)

    # Call our handler to get the original return address. The handler will
    # look up the return address in the shadow stack based on the stack pointer
    # we passed.
    call nwind_on_ret_trampoline

    # The handler returns the original return address in RAX. We move it to RSI
    # because RSI is caller-saved and we're about to restore RAX.
    mov rsi, rax

    # Restore the stack alignment and all registers in reverse order of pushing.
    # This returns the stack and registers to their original state.
    add rsp, 8
    pop rcx
    pop rdx
    pop rax

    # Finally, jump to the original return address we got from the handler.
    # We use RSI because we saved the address there after getting it from
    # the handler.
    jmp rsi
