.section	__TEXT,__text,regular,pure_instructions
.build_version macos, 14, 0	sdk_version 15, 1
.p2align	2
.globl _nwind_ret_trampoline_start
.private_extern _nwind_ret_trampoline_start

_nwind_ret_trampoline_start:
.cfi_startproc
.cfi_personality 155, ___gxx_personality_v0
.cfi_lsda 16,LLSDA0
.cfi_undefined lr
.cfi_endproc
LEHB0:
    nop
LEHE0:

.globl _nwind_ret_trampoline
.private_extern _nwind_ret_trampoline
_nwind_ret_trampoline:
    /* Save the original return value */
    sub sp, sp, #64     // 8 * 8
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    
    mov x0, sp
    add x0, x0, #64
    bl _nwind_on_ret_trampoline
    
    /* Restore the original return address */
    mov lr, x0
    
    /* Restore the original return value */
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    add sp, sp, #64
    
    /* Return */
    ret

L3:
    bl _nwind_on_exception_through_trampoline
    /* Restore the real return address */
    mov lr, x0
    b ___cxa_rethrow


/* Exception handling data */
.section __TEXT,__gcc_except_tab
.align 2
LLSDA0:
    .byte 0xff
    .byte 0x9b
    .uleb128 LLSDATT0-LLSDATTD0
LLSDATTD0:
    .byte 0x1
    .uleb128 LLSDACSE0-LLSDACSB0
LLSDACSB0:
    .uleb128 LEHB0-_nwind_ret_trampoline_start
    .uleb128 LEHE0-LEHB0
    .uleb128 L3-_nwind_ret_trampoline_start
    .uleb128 0x1
LLSDACSE0:
    .byte 0x1
    .byte 0
    .align 2
    .long 0
LLSDATT0:

/* Symbol references */
.section __DATA,__data
.align 3
.private_extern ___gxx_personality_v0

.subsections_via_symbols
