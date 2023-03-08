#include "kernel/kernel.h" /* configures the kernel */

/* sections */

#include <machine/vm.h>
#include "kernel/kernel.h"
#include <minix/config.h>
#include <minix/const.h>
#include <minix/com.h>
#include <machine/asm.h>
#include <machine/interrupt.h>
#include "archconst.h"
#include "sconst.h"
#include <machine/multiboot.h>

#include "arch_proto.h" /* K_STACK_SIZE */

#ifdef CONFIG_SMP
#include "kernel/smp.h"
#endif

/* Selected 386 tss offsets. */
#define TSS3_S_SP0	4

IMPORT(copr_not_available_handler)
IMPORT(params_size)
IMPORT(params_offset)
IMPORT(mon_ds)
IMPORT(switch_to_user)
IMPORT(multiboot_init)

.text
/*===========================================================================*/
/*				MINIX				     */
/*===========================================================================*/
.global MINIX
MINIX:
/* this is the entry point for the MINIX kernel */
	jmp multiboot_init

/* Multiboot header here*/

.balign 8

#define MULTIBOOT_FLAGS (MULTIBOOT_HEADER_WANT_MEMORY | MULTIBOOT_HEADER_MODS_ALIGNED | MULTIBOOT_HEADER_HAS_VBE)

_C_LABEL(Multiboot_Header):
multiboot_magic:
	.long MULTIBOOT_HEADER_MAGIC
multiboot_flags:
	.long MULTIBOOT_FLAGS
multiboot_checksum:
	.long -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_FLAGS)
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
/* Video mode */
multiboot_mode_type:
	.long MULTIBOOT_VIDEO_MODE_EGA
multiboot_width:
	.long MULTIBOOT_CONSOLE_COLS
multiboot_height:
	.long MULTIBOOT_CONSOLE_LINES
multiboot_depth:
	.long 0
	
	.align	8
	.globl	Multiboot2_Header
_C_LABEL(Multiboot2_Header):
	.long	MULTIBOOT2_HEADER_MAGIC
	.long	MULTIBOOT2_ARCHITECTURE_I386
	.long	Multiboot2_Header_end - Multiboot2_Header
	.long	-(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_ARCHITECTURE_I386 \
		+ (Multiboot2_Header_end - Multiboot2_Header))

	.long	1	/* MULTIBOOT_HEADER_TAG_INFORMATION_REQUEST */
	.long	16	/* sizeof(multiboot_header_tag_information_request) */
			/* + sizeof(uint32_t) * requests */
	.long	4	/* MULTIBOOT_TAG_TYPE_BASIC_MEMINFO */
	.long	6	/* MULTIBOOT_TAG_TYPE_MMAP */

	.long	8	/* MULTIBOOT_HEADER_TAG_ENTRY_ADDRESS_EFI32 */
	.long	12	/* sizeof(struct multiboot_tag_efi32) */
	.long	multiboot2_loader
	.long   0	/* pad for 8 bytes alignment */

	.long	4	/* MULTIBOOT_HEADER_TAG_CONSOLE_FLAGS */
	.long	12	/* sizeof(struct multiboot_header_tag_console_flags) */
	.long	3	/* MULTIBOOT_CONSOLE_FLAGS_CONSOLE_REQUIRED || */
				/*  MULTIBOOT_CONSOLE_FLAGS_EGA_TEXT_SUPPORTED */
	.long   0	/* pad for 8 bytes alignment */
	
	.long	0	/* MULTIBOOT_HEADER_TAG_END */
	.long	8	/* sizeof(struct multiboot_tag) */
	
	.globl	Multiboot2_Header_end
_C_LABEL(Multiboot2_Header_end):

multiboot_init:
	mov	$load_stack_start, %esp	/* make usable stack */
	mov	$0, %ebp
	push	$0	/* set flags to known good state */
	popf	/* esp, clear nested task and int enable */
	push	$0
	cmpl	$MULTIBOOT_INFO_MAGIC,%eax
	je	multiboot1_loader

	cmpl	$MULTIBOOT2_BOOTLOADER_MAGIC,%eax
	je	multiboot2_loader
	jmp		hang

multiboot1_loader:
        push    %ebx	/* multiboot information struct */
	push	%eax	/* multiboot magic number */
        call    _C_LABEL(pre_init)

	/* Kernel is mapped high now and ready to go, with
	 * the boot info pointer returnd in %eax. Set the
	 * highly mapped stack, initialize it, push the boot
	 * info pointer and jump to the highly mapped kernel.
	 */
	mov	$k_initial_stktop, %esp
	push	$0	/* Terminate stack */
	push	%eax
        call    _C_LABEL(kmain)

	/* not reached */
hang:
	hlt
	jmp hang
	
multiboot2_loader:
	push    %ebx	/* multiboot2 information struct */
	push	%eax	/* multiboot2 magic number */
	call    _C_LABEL(pre2_init)
	jmp hang

.data
load_stack:
	.space 4096
load_stack_start:
