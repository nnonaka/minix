/* $NetBSD: console.c,v 1.2 2018/09/15 16:44:15 jmcneill Exp $ */

/*-
 * Copyright (c) 2018 Jared McNeill <jmcneill@invisible.ca>
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

#include "efiboot.h"

static EFI_INPUT_KEY key_cur;
static int key_pending;

static int console_mode = 0;

#define CONSDEV_COM0	0

static SERIAL_IO_INTERFACE *serios[4];
static int default_comspeed =
#if defined(CONSPEED)
    CONSPEED;
#else
    115200;
#endif
static int com_dev = CONSDEV_COM0;

static u_char serbuf[16];
static int serbuf_read = 0;
static int serbuf_write = 0;

static void efi_com_probe(void);
static bool efi_valid_com(int);
static int efi_com_init(int, int);
static int efi_com_getc(void);
static int efi_com_putc(int);
static int efi_com_ischar(void);

static void efi_text_init(void);
static int efi_kbd_getc(void);
static int efi_kbd_ischar(void);
static void efi_text_putc(int);

void
cninit(void)
{
	console_mode = 0;
	efi_text_init();
	efi_com_probe();
	efi_com_init(com_dev, default_comspeed);
}

void
efi_text_init()
{
	uefi_call_wrapper(ST->ConOut->Reset, 2, ST->ConOut, TRUE);
	uefi_call_wrapper(ST->ConOut->SetMode, 2, ST->ConOut, 0);
	uefi_call_wrapper(ST->ConOut->EnableCursor, 2, ST->ConOut, TRUE);
}

int
getchar(void)
{
	if (console_mode == 0)
		return efi_kbd_getc();
	else
		return efi_com_getc();
}

int
ischar(void)
{
	if (console_mode == 0)
		return efi_kbd_ischar();
	else
		return efi_com_ischar();
}

void
putchar(int c)
{
	if (console_mode == 0)
		efi_text_putc(c);
	else
		efi_com_putc(c);
}

int
efi_kbd_getc(void)
{
	EFI_STATUS status;
	EFI_INPUT_KEY key;

	if (key_pending) {
		key = key_cur;
		key_pending = 0;
	} else {
		status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
		while (status == EFI_NOT_READY) {
			if (ST->ConIn->WaitForKey != NULL)
				WaitForSingleEvent(ST->ConIn->WaitForKey, 0);
			status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
		}
	}

	return key.UnicodeChar;
}

void
efi_text_putc(int c)
{
	CHAR16 buf[2] = { c, '\0' };
	if (c == '\n')
		efi_text_putc('\r');
	uefi_call_wrapper(ST->ConOut->OutputString, 2, ST->ConOut, buf);
}

int
efi_kbd_ischar(void)
{
	EFI_INPUT_KEY key;
	EFI_STATUS status;

	if (ST->ConIn->WaitForKey == NULL) {
		if (key_pending)
			return 1;
		status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
		if (status == EFI_SUCCESS) {
			key_cur = key;
			key_pending = 1;
		}
		return key_pending;
	} else {
		status = uefi_call_wrapper(BS->CheckEvent, 1, ST->ConIn->WaitForKey);
		return status == EFI_SUCCESS;
	}
}

/*
 * serial port
 */
static void
efi_com_probe(void)
{
	EFI_STATUS status;
	UINTN i, nhandles;
	EFI_HANDLE *handles;
	EFI_DEVICE_PATH	*dp, *dp0;
	EFI_DEV_PATH_PTR dpp;
	SERIAL_IO_INTERFACE *serio;
	int uid = -1;

	status = LibLocateHandle(ByProtocol, &SerialIoProtocol, NULL,
	    &nhandles, &handles);
	if (EFI_ERROR(status))
		return;

	for (i = 0; i < nhandles; i++) {
		/*
		 * Identify port number of the handle.  This assumes ACPI
		 * UID 0-3 map to legacy COM[1-4] and they use the legacy
		 * port address.
		 */
		status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[i],
		    &DevicePathProtocol, (void **)&dp0);
		if (EFI_ERROR(status))
			continue;

		for (uid = -1, dp = dp0;
		     !IsDevicePathEnd(dp);
		     dp = NextDevicePathNode(dp)) {

			if (DevicePathType(dp) == ACPI_DEVICE_PATH &&
			    DevicePathSubType(dp) == ACPI_DP) {
				dpp = (EFI_DEV_PATH_PTR)dp;
				if (dpp.Acpi->HID == EISA_PNP_ID(0x0501)) {
					uid = dpp.Acpi->UID;
					break;
				}
			}
		}
		if (uid < 0 || __arraycount(serios) <= uid)
			continue;

		/* Prepare SERIAL_IO_INTERFACE */
		status = uefi_call_wrapper(BS->HandleProtocol, 3, handles[i],
		    &SerialIoProtocol, (void **)&serio);
		if (EFI_ERROR(status))
			continue;

#if 0
		/* NN: It seems UEFI serial not working */
		serios[uid] = serio;
#else
		serios[uid] = NULL;
#endif
	}

	FreePool(handles);

}

static bool
efi_valid_com(int dev)
{
	int idx;

	idx = dev - CONSDEV_COM0;

	return idx < __arraycount(serios) &&
	    serios[idx] != NULL;
}

static int
efi_com_init(int dev, int speed)
{
	EFI_STATUS status;
	SERIAL_IO_INTERFACE *serio;

	if (speed <= 0)
		return 0;

	if (!efi_valid_com(dev))
		return 0;

	serio = serios[dev - CONSDEV_COM0];
		
	if (serio->Mode->BaudRate != speed) {
		status = uefi_call_wrapper(serio->SetAttributes, 7, serio,
		    speed, serio->Mode->ReceiveFifoDepth,
		    serio->Mode->Timeout, serio->Mode->Parity,
		    serio->Mode->DataBits, serio->Mode->StopBits);
		if (EFI_ERROR(status)) {
			printf("com%d: SetAttribute() failed with status=%" PRIxMAX
			    "\n", dev - CONSDEV_COM0, (uintmax_t)status);
			return 0;
		}
	}

	default_comspeed = speed;
	memset(serbuf, 0, sizeof(serbuf));
	serbuf_read = serbuf_write = 0;

	return speed;
}

static int
efi_com_getc(void)
{
	EFI_STATUS status;
	SERIAL_IO_INTERFACE *serio;
	UINTN sz;
	u_char c;

	if (!efi_valid_com(com_dev))
		panic("Invalid serial port: com_dev=%d", com_dev);

	if (serbuf_read != serbuf_write) {
		c = serbuf[serbuf_read];
		serbuf_read = (serbuf_read + 1) % __arraycount(serbuf);
		return c;
	}

	serio = serios[com_dev - CONSDEV_COM0];

	for (;;) {
		sz = 1;
		status = uefi_call_wrapper(serio->Read, 3, serio, &sz, &c);
		if (!EFI_ERROR(status) && sz > 0)
			break;
		if (status != EFI_TIMEOUT && EFI_ERROR(status))
			panic("Error reading from serial status=%"PRIxMAX,
			    (uintmax_t)status);
	}
	return c;
}

static int
efi_com_putc(int c)
{
	EFI_STATUS status;
	SERIAL_IO_INTERFACE *serio;
	UINTN sz = 1;
	u_char buf;

	if (!efi_valid_com(com_dev))
		panic("Invalid serial port: com_dev=%d", com_dev);

	serio = serios[com_dev - CONSDEV_COM0];

	buf = c;
	status = uefi_call_wrapper(serio->Write, 3, serio, &sz, &buf);
	if (EFI_ERROR(status) || sz < 1)
		return 0;
	return 1;
}

static int
efi_com_ischar(void)
{
	EFI_STATUS status;
	SERIAL_IO_INTERFACE *serio;
	UINTN sz;
	u_char c;

	if (!efi_valid_com(com_dev))
		panic("Invalid serial port: com_dev=%d", com_dev);

	if (serbuf_read != serbuf_write)
		return 1;

	serio = serios[com_dev - CONSDEV_COM0];
	sz = 1;
	status = uefi_call_wrapper(serio->Read, 3, serio, &sz, &c);
	if (EFI_ERROR(status) || sz < 1)
		return 0;

	serbuf[serbuf_write] = c;
	serbuf_write = (serbuf_write + 1) % __arraycount(serbuf);
	return 1;
}
