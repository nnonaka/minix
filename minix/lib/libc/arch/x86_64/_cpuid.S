/* _cpuid() - interface to cpuid instruction (x86_64) */

/* void _cpuid(u32_t *eax, u32_t *ebx, u32_t *ecx, u32_t *edx); */

#include <machine/asm.h>

ENTRY(_cpuid)
	/* Save callee-saved registers used to hold the four pointer args.
	 * cpuid clobbers rax/rbx/rcx/rdx; r12-r15 survive it. */
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	/* Stash output pointers in callee-saved registers. */
	movq	%rdi, %r12		/* *eax */
	movq	%rsi, %r13		/* *ebx */
	movq	%rdx, %r14		/* *ecx */
	movq	%rcx, %r15		/* *edx */

	/* Load cpuid inputs from the caller-supplied pointers. */
	movl	(%rdi), %eax
	movl	(%rsi), %ebx
	movl	(%rdx), %ecx
	movl	(%rcx), %edx

	cpuid

	/* Store cpuid results back through the saved pointers. */
	movl	%eax, (%r12)
	movl	%ebx, (%r13)
	movl	%ecx, (%r14)
	movl	%edx, (%r15)

	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	ret
