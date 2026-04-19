/*	$NetBSD: multiboot32.S,v 1.1.2.3 2019/09/27 09:20:16 martin Exp $ */

#include <machine/asm.h>
//#include <machine/specialreg.h>

/*
 * multiboot32(entry 4(%esp), multiboot2_info 8(%esp), magic 12(%esp))
 */
ENTRY(multiboot32_start)
start:
        movl    12(%esp),%eax
        movl    8(%esp),%ebx
        movl    4(%esp),%edx
        jmp    *%edx
multiboot32_end:
