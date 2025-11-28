/**
 * GhostStack Return Trampoline - x86_64 macOS (Darwin)
 * =====================================================
 *
 * This assembly implements the return address trampoline for shadow stack unwinding
 * on Intel-based macOS systems.
 *
 * When GhostStack patches a return address to point here, this trampoline:
 *   1. Saves the function's return value (preserved across the callback)
 *   2. Calls _ghost_trampoline_handler() to get the real return address
 *   3. Restores the return value and jumps to the real return address
 *
 * macOS/Darwin Differences from Linux:
 *   - Symbols are prefixed with underscore (_ghost_ret_trampoline vs ghost_ret_trampoline)
 *   - Uses Mach-O object format instead of ELF
 *   - Section names differ (__TEXT,__text vs .text)
 *   - Exception table goes in __TEXT,__gcc_except_tab
 *   - Uses .private_extern instead of .hidden
 *   - No .type directive (Mach-O doesn't use it)
 *   - Uses ___gxx_personality_v0 and ___cxa_rethrow (triple underscore)
 *
 * x86_64 SysV ABI Notes (same on macOS):
 *   - Return values: rax (integer/pointer), rdx (second value), xmm0/xmm1 (floating point)
 *   - We save rax, rdx, and rcx (used by some ABIs like Rust for extra return values)
 *   - Stack must be 16-byte aligned before CALL instruction
 */

.section	__TEXT,__text,regular,pure_instructions
.build_version macos, 10, 15	sdk_version 15, 1
.p2align 4

/* ==========================================================================
 * _ghost_ret_trampoline_start - Exception handling anchor
 * ==========================================================================
 * This symbol marks the start of the function for DWARF unwinding purposes.
 * The CFI directives set up exception handling:
 *   - .cfi_personality: Use ___gxx_personality_v0 for C++ exceptions
 *   - .cfi_lsda: Point to our Language Specific Data Area for catch clauses
 *   - .cfi_undefined rip: Signal that return address is non-standard
 */
.globl _ghost_ret_trampoline_start
.private_extern _ghost_ret_trampoline_start

_ghost_ret_trampoline_start:
LFB0:
.cfi_startproc
.cfi_personality 155, ___gxx_personality_v0
.cfi_lsda 16, LLSDA0
.cfi_undefined %rip

/* Exception try region starts here - any exception in this region
 * will be caught and redirected to L3 for proper handling */
LEHB0:
    nop                         /* Placeholder for exception region start */
LEHE0:

/* ==========================================================================
 * _ghost_ret_trampoline - The actual trampoline entry point
 * ==========================================================================
 * When a function returns and its return address has been patched to point
 * here, execution continues at this label. The original return address is
 * stored in GhostStack's shadow stack and will be retrieved via callback.
 */
.globl _ghost_ret_trampoline
.private_extern _ghost_ret_trampoline
_ghost_ret_trampoline:

    /* -------------------------------------------------------------------------
     * Step 1: Save return values
     * -------------------------------------------------------------------------
     * The function we're returning from may have placed values in these registers.
     * We must preserve them across our callback to _ghost_trampoline_handler().
     *
     * Stack layout after saves:
     *   rsp+24: original rsp (return address location)
     *   rsp+16: saved rax (primary return value)
     *   rsp+8:  saved rdx (secondary return value, e.g., for 128-bit returns)
     *   rsp:    saved rcx (used by Rust ABI, also scratch in some cases)
     *   [then -8 for alignment]
     */
    pushq %rax                    /* Save primary return value */
    pushq %rdx                    /* Save secondary return value */
    pushq %rcx                    /* Save rcx (Rust ABI uses this) */

    /* Align stack to 16-byte boundary (required by SysV ABI before CALL).
     * We've pushed 3 * 8 = 24 bytes. Adding 8 makes it 32, which is aligned. */
    subq $8, %rsp

    /* -------------------------------------------------------------------------
     * Step 2: Call into C++ to get the real return address
     * -------------------------------------------------------------------------
     * Argument (rdi): Pointer to where the return address *would* be on stack
     *   = rsp + 8 (alignment) + 8 (rcx) + 8 (rdx) + 8 (rax) = rsp + 32
     * This lets the C++ code verify stack pointer consistency if desired.
     *
     * _ghost_trampoline_handler() returns the real return address in rax.
     */
    movq %rsp, %rdi
    addq $32, %rdi                 /* rdi = &original_return_addr_location */
    callq _ghost_trampoline_handler

    /* -------------------------------------------------------------------------
     * Step 3: Restore and jump to real return address
     * -------------------------------------------------------------------------
     * rax now contains the real return address. Move it to rsi (callee-saved
     * across our restores), restore the original return values, then jump.
     */
    movq %rax, %rsi                /* Save real return address */
    addq $8, %rsp                  /* Remove alignment padding */
    popq %rcx                     /* Restore rcx */
    popq %rdx                     /* Restore secondary return value */
    popq %rax                     /* Restore primary return value */
    jmpq *%rsi                     /* Jump to real return address */

/* ==========================================================================
 * Exception landing pad
 * ==========================================================================
 * If an exception is thrown while executing in the try region (LEHB0-LEHE0),
 * the C++ runtime's personality function sees our LSDA entry and directs
 * unwinding here. We call the exception handler and rethrow.
 */
L3:
    movq %rax, %rdi          /* Exception object pointer -> first argument */
    callq _ghost_exception_handler

    /* rax = real return address. Push it onto stack so the unwinder
     * sees correct return address when ___cxa_rethrow continues. */
    pushq %rax

    /* Rethrow the exception - unwinding continues from real return address */
    jmp ___cxa_rethrow

.cfi_endproc
LFE0:

/* ==========================================================================
 * LSDA (Language Specific Data Area)
 * ==========================================================================
 * This data tells ___gxx_personality_v0 how to handle exceptions in our code.
 * Format: DWARF exception handling tables
 *
 * Key fields:
 *   - Call site table: Maps PC ranges to landing pads
 *   - Action table: What to do when landing (0 = cleanup, >0 = catch)
 *   - Type table: Exception types to catch (not used here, we catch all)
 */
.section __TEXT,__gcc_except_tab
.align 2
LLSDA0:
    .byte 0xff                  /* @LPStart encoding: omit (use function start) */
    .byte 0x9b                  /* @TType encoding: indirect pcrel sdata4 */
    .uleb128 LLSDATT0-LLSDATTD0 /* @TType base offset */
LLSDATTD0:
    .byte 0x1                   /* Call site encoding: uleb128 */
    .uleb128 LLSDACSE0-LLSDACSB0    /* Call site table length */
LLSDACSB0:
    /* Call site entry: try region that catches exceptions */
    .uleb128 LEHB0-LFB0         /* Region start (relative to function) */
    .uleb128 LEHE0-LEHB0        /* Region length */
    .uleb128 L3-LFB0            /* Landing pad (where to go on exception) */
    .uleb128 0x1                /* Action: index into action table (catch-all) */
LLSDACSE0:
    .byte 0x1                   /* Action table entry: catch type index 1 */
    .byte 0                     /* No next action */
    .align 2
    .long 0                     /* Type table entry: 0 = catch(...) */
LLSDATT0:

/* ==========================================================================
 * Symbol declarations
 * ==========================================================================
 * Declare reference to the C++ personality function.
 * On macOS, this is ___gxx_personality_v0 (three underscores total).
 */
.section __DATA,__data
.align 3
.private_extern ___gxx_personality_v0

/* Enable dead code stripping optimization */
.subsections_via_symbols
