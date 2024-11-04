	.file	"trampoline_template.c"
	.text
	.globl	nwind_ret_trampoline
	.type	nwind_ret_trampoline, @function
nwind_ret_trampoline:
	movq	%rbp, %rax
	movq	%rax, -24(%rbp)
	movq	-24(%rbp), %rax
	movq	%rax, %rdi
	call	nwind_on_ret_trampoline@PLT
	movq	%rax, %rbx
	movq	%rbx, %rax
	call	*%rax
	nop
	ud2
	.size	nwind_ret_trampoline, .-nwind_ret_trampoline
	.ident	"GCC: (GNU) 14.2.1 20240910"
	.section	.note.GNU-stack,"",@progbits
