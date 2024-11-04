.intel_syntax noprefix
.text
.global nwind_ret_trampoline
.type nwind_ret_trampoline, @function
nwind_ret_trampoline:
    push rax        # Save return value
    push rdx
    push rcx
    sub rsp, 8      # Align stack
    mov rdi, rsp    # Pass stack pointer as argument
    add rdi, 32     # Adjust for pushed values (3 registers + alignment)
    call nwind_on_ret_trampoline
    mov rsi, rax    # Get return address
    add rsp, 8      # Restore stack
    pop rcx
    pop rdx
    pop rax
    jmp rsi         # Jump to original return address