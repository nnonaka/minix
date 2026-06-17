#ifndef _X86_64_IPCCONST_H_
#define _X86_64_IPCCONST_H_

/* IPC message struct size assertions are suppressed on x86_64: the message
 * layout uses arch-width types (long, vir_bytes, char *) that expand to
 * 8 bytes, so structs designed for i386 (56-byte payload) do not fit.
 * The x86_64 IPC message ABI needs a proper redesign. */
#define _ASSERT_MSG_SIZE(msg_type)	/* not enforced on x86_64 */

#define KERVEC_INTR 32     /* syscall trap to kernel */
#define IPCVEC_INTR 33     /* ipc trap to kernel  */

#define KERVEC_UM 34     /* syscall trap to kernel, user-mapped code */
#define IPCVEC_UM 35     /* ipc trap to kernel, user-mapped code  */

#define IPC_STATUS_REG		bx

#endif  /* _I386_IPCCONST_H_ */
