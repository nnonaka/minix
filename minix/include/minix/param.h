
#ifndef _MINIX_PARAM_H
#define _MINIX_PARAM_H 1

#include <minix/com.h>
#include <minix/const.h>

/* Number of processes contained in the system image. */
#define NR_BOOT_PROCS   (NR_TASKS + LAST_SPECIAL_PROC_NR + 1)

#ifdef _MINIX_SYSTEM

#if 0
/* Copied from multiboot.h */
struct multiboot1_info {
	uint32_t	mi_flags;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_MEMORY. */
	uint32_t	mi_mem_lower;
	uint32_t	mi_mem_upper;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_BOOT_DEVICE. */
	uint8_t		mi_boot_device_part3;
	uint8_t		mi_boot_device_part2;
	uint8_t		mi_boot_device_part1;
	uint8_t		mi_boot_device_drive;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_CMDLINE. */
	char *		mi_cmdline;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_MODS. */
	uint32_t	mi_mods_count;
	vaddr_t		mi_mods_addr;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_{AOUT,ELF}_SYMS. */
	uint32_t	mi_elfshdr_num;
	uint32_t	mi_elfshdr_size;
	vaddr_t		mi_elfshdr_addr;
	uint32_t	mi_elfshdr_shndx;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_MMAP. */
	uint32_t	mi_mmap_length;
	vaddr_t		mi_mmap_addr;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_DRIVES. */
	uint32_t	mi_drives_length;
	vaddr_t		mi_drives_addr;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_CONFIG_TABLE. */
	void *		unused_mi_config_table;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_LOADER_NAME. */
	char *		mi_loader_name;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_APM. */
	void *		unused_mi_apm_table;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_VBE. */
	void *		unused_mi_vbe_control_info;
	void *		unused_mi_vbe_mode_info;
	uint16_t	unused_mi_vbe_mode;
	uint16_t	unused_mi_vbe_interface_seg;
	uint16_t	unused_mi_vbe_interface_off;
	uint16_t	unused_mi_vbe_interface_len;

	/* Valid if mi_flags sets MULTIBOOT_INFO_HAS_FRAMEBUFFER. */
	uint64_t	framebuffer_addr;
	uint32_t	framebuffer_pitch;
	uint32_t	framebuffer_width;
	uint32_t	framebuffer_height;
	uint8_t		framebuffer_bpp;
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 	0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB     	1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT     2
	uint8_t framebuffer_type;
	union {
		struct {
			uint32_t framebuffer_palette_addr;
			uint16_t framebuffer_palette_num_colors;
		};
		struct {
			uint8_t framebuffer_red_field_position;
			uint8_t framebuffer_red_mask_size;
			uint8_t framebuffer_green_field_position;
			uint8_t framebuffer_green_mask_size;
			uint8_t framebuffer_blue_field_position;
			uint8_t framebuffer_blue_mask_size;
		};
	};

};
#endif

struct my_multiboot_mmap {
	uint32_t	mm_size;
	uint64_t	mm_base_addr;
	uint64_t	mm_length;
	uint32_t	mm_type;
};

struct my_multiboot_module {
	uint32_t	mod_start;
	uint32_t	mod_end;
	char *		mod_string;
	uint32_t	mod_reserved;
};

#define MULTIBOOT_MAX_MODS     20
#define MULTIBOOT_PARAM_BUF_SIZE 1024

//typedef struct multiboot1_info multiboot_info_t;
typedef struct my_multiboot_module multiboot_module_t;
typedef struct my_multiboot_mmap multiboot_memory_map_t;

/* This is used to obtain system information through SYS_GETINFO. */
#define MAXMEMMAP 40
typedef struct kinfo {
        /* Straight multiboot-provided info */
        //multiboot_info_t        mbi1;
        uint32_t	            module_count;
        multiboot_module_t      module_list[MULTIBOOT_MAX_MODS];
        multiboot_memory_map_t  memmap[MAXMEMMAP]; /* free mem list */
        phys_bytes              mem_high_phys;
        int                     mmap_size;

        /* Multiboot-derived */
        int                     mods_with_kernel; /* no. of mods incl kernel */
        int                     kern_mod; /* which one is kernel */

        /* Minix stuff, started at bootstrap phase */
        int                     freepde_start;  /* lowest pde unused kernel pde */
        char                    param_buf[MULTIBOOT_PARAM_BUF_SIZE];

        /* Minix stuff */
        struct kmessages *kmessages;
        int do_serial_debug;    /* system serial output */
        int serial_debug_baud;  /* serial baud rate */
        int minix_panicing;     /* are we panicing? */
        vir_bytes               user_sp; /* where does kernel want stack set */
        vir_bytes               user_end; /* upper proc limit */
        vir_bytes               vir_kern_start; /* kernel addrspace starts */
        vir_bytes               bootstrap_start, bootstrap_len;
        struct boot_image       boot_procs[NR_BOOT_PROCS];
        int nr_procs;           /* number of user processes */
        int nr_tasks;           /* number of kernel tasks */
        char release[6];        /* kernel release number */
        char version[6];        /* kernel version number */
	int vm_allocated_bytes; /* allocated by kernel to load vm */
	int kernel_allocated_bytes;		/* used by kernel */
	int kernel_allocated_bytes_dynamic;	/* used by kernel (runtime) */
} kinfo_t;
#endif /* _MINIX_SYSTEM */

#endif
