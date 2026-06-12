/* x86_64 signal return trampoline for MINIX.
 *
 * On x86_64, the kernel pushes the sigcontext pointer (scp) onto the user
 * stack just below the trampoline return address before entering the signal
 * handler.  When the handler executes ret, execution arrives here with:
 *   (%rsp) = scp pointer   (placed by kernel above sf_sc)
 * Load it into %rdi (SysV AMD64 first-arg register) and tail-call sigreturn.
 */
#include <machine/asm.h>

ENTRY(__sigreturn)
	movq	(%rsp), %rdi		/* scp pointer left by kernel */
	jmp	PIC_PLT(_C_LABEL(sigreturn))
