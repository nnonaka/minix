/*	$NetBSD: efiboot_machdep.h,v 1.1 2017/01/24 11:09:14 nonaka Exp $	*/

/*-
 * Copyright (c) 2016 Kimihiro Nonaka <nonaka@netbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
 
typedef unsigned long physaddr_t;


void startprog(physaddr_t, uint32_t, uint32_t *, physaddr_t);
void multiboot(physaddr_t, physaddr_t, physaddr_t, uint32_t);

int exec_multiboot(const char *, const char *);

/* multiboot */
struct multiboot_package;
struct multiboot_package_priv;

struct multiboot_package {
	int			 mbp_version;
	struct multiboot_header	*mbp_header;
	const char		*mbp_file;
	char			*mbp_args;
	u_long			 mbp_basemem;
	u_long			 mbp_extmem;
	u_long			 mbp_loadaddr;
	u_long			*mbp_marks;
	struct multiboot_package_priv*mbp_priv;
};

struct multiboot_package *probe_multiboot2(const char *);

void efi_dcache_flush(u_long, u_long);
void efi_boot_kernel(u_long[]);
void efi_md_init(void);
void efi_md_show(void);
