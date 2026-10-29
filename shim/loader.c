/*
 * Milestone 4: act as iBoot for xnu.
 *
 * mkpayload.py already resolved the kernel's chained fixups and built the
 * ADT, so this only has to: find the payload in the ABL ramdisk, copy the
 * segments to phys_base + (vmaddr - virt_base), place the ADT and
 * boot_args behind the kernel, and jump to the entry point with the MMU
 * and caches off, x0 = boot_args (physical).
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "loader.h"

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == 0x2a8, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

static int magic_ok(const char *m)
{
	static const char want[] = "SWEETXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

const char *loader_boot(uintptr_t dtb, const char *extra)
{
	uint64_t rd_start, rd_end;

	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
		return "no initrd in /chosen";
	con_hex2("INITRD", "lo=", rd_start, "hi=", rd_end);

	const struct payload *p = (const struct payload *)rd_start;
	if (!magic_ok(p->magic))
		return "no SWEETXNU payload";
	if (p->version != 1 || (p->adt_off & 7) || (rd_start & 7))
		return "bad payload header";

	uint64_t pa_of_va_base = p->phys_base - p->virt_base;	/* pa = va + this */
	con_hex2("PHYS", "base=", p->phys_base, "size=", p->mem_size);
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);

	/* The payload must not overlap where the kernel goes. */
	if (rd_end > p->phys_base && rd_start < p->phys_base + p->mem_size)
		return "ramdisk overlaps kernel memory";

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va_base;
		copy64(dst, rd_start + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero64(dst + ((s->file_size + 7) & ~7UL), s->vmsize - ((s->file_size + 7) & ~7UL));
	}
	con_hex2("SEGS", "n=", p->nsegs, "end=", p->kernel_end + pa_of_va_base);

	/* ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va_base;
	copy64(adt_pa, rd_start + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va_base;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	/* the menu's boot-args (iMiatoll UEFI), appended after the payload's own; later args win in PE_parse_boot_argn */
	{
		int n = 0;
		while (n < BOOT_LINE_LENGTH - 1 && ba->CommandLine[n])
			n++;
		for (; extra && *extra && n < BOOT_LINE_LENGTH - 1; extra++)
			ba->CommandLine[n++] = *extra;
		ba->CommandLine[n] = 0;
	}
	ba->memSizeActual = p->mem_size;
	con_hex2("BOOTARG", "pa=", ba_pa, "top=", top);

	uintptr_t entry_pa = p->entry_va + pa_of_va_base;
	con_hex("JUMP", entry_pa);
	con_str("XNU", "jumping (fbcon clears screen)");

	/* Everything was written with the MMU and caches off; drop any stale I-cache lines too. */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}

/* 64 bytes at the very end of the ramdisk (tools/mkdual.py): where the Android kernel and its ramdisk are in it */
struct lnx_trailer {
	char magic[8];			/* "SWEETLNX" */
	uint64_t kernel_off, kernel_size, ramdisk_off, ramdisk_size;	/* from the start of the ramdisk */
	uint64_t reserved[3];
};

/* free RAM below 4 GiB between the reserved regions of the device tree (0x93b17000..0x9c000000, the splash buffer is above) */
#define ANDROID_KERNEL_BASE	0x94000000UL	/* 2 MiB aligned; the Image goes at +text_offset */
#define ANDROID_INITRD_BASE	0x98000000UL
#define ANDROID_LIMIT		0x9b000000UL

typedef void (*linux_entry_t)(uint64_t dtb, uint64_t x1, uint64_t x2, uint64_t x3);

/* Drop whatever the bootloader left in the caches for a range we are about to fill (caches are off for our own accesses). */
static void inval_range(uintptr_t a, uint64_t n)
{
	for (uintptr_t p = a & ~63UL; p < a + n; p += 64)
		__asm__ volatile("dc ivac, %0" :: "r"(p) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

static void flush_range(uintptr_t a, uint64_t n)
{
	for (uintptr_t p = a & ~63UL; p < a + n; p += 64)
		__asm__ volatile("dc civac, %0" :: "r"(p) : "memory");
	__asm__ volatile("dsb sy" ::: "memory");
}

const char *android_boot(uintptr_t dtb)
{
	uint64_t rd_start, rd_end;

	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end))
		return "no initrd in /chosen";
	if (rd_end < rd_start + sizeof(struct lnx_trailer))
		return "ramdisk too small";
	const struct lnx_trailer *t = (const struct lnx_trailer *)(rd_end - sizeof(struct lnx_trailer));
	const char want[8] = { 'S', 'W', 'E', 'E', 'T', 'L', 'N', 'X' };
	for (int i = 0; i < 8; i++)
		if (t->magic[i] != want[i])
			return "this image has no Android in it (build it with tools/mkdual.py)";
	uintptr_t img = rd_start + t->kernel_off;
	uint64_t ksize = t->kernel_size, rsize = t->ramdisk_size;
	uint64_t text_off = *(volatile uint64_t *)(img + 8);
	uint64_t image_size = *(volatile uint64_t *)(img + 16);
	uint32_t arm_magic = *(volatile uint32_t *)(img + 56);
	con_hex2("ANDROID", "kernel=", img, "size=", ksize);
	if (arm_magic != 0x644d5241)
		return "the Android kernel is not an arm64 Image";
	if (text_off >= 0x200000 || image_size < ksize)
		return "odd Android Image header";
	uintptr_t kdst = ANDROID_KERNEL_BASE + text_off;
	if (kdst + image_size > ANDROID_INITRD_BASE || ANDROID_INITRD_BASE + rsize > ANDROID_LIMIT)
		return "Android does not fit the free RAM";
	uint64_t dsz = fdt_totalsize(dtb);
	if (!dsz)
		return "no device tree";
	/* nothing we copy from or to may overlap */
	if ((rd_start < ANDROID_LIMIT && rd_end > ANDROID_KERNEL_BASE) ||
	    (dtb < ANDROID_LIMIT && dtb + dsz > ANDROID_KERNEL_BASE))
		return "the ramdisk or the device tree sits in the free RAM";
	con_hex2("DST", "kernel=", kdst, "initrd=", ANDROID_INITRD_BASE);

	inval_range(ANDROID_KERNEL_BASE, image_size + text_off);
	inval_range(ANDROID_INITRD_BASE, rsize);
	copy64(kdst, img, ksize);
	copy64(ANDROID_INITRD_BASE, rd_start + t->ramdisk_off, rsize);
	if (!fdt_chosen_set_u64(dtb, "linux,initrd-start", ANDROID_INITRD_BASE) ||
	    !fdt_chosen_set_u64(dtb, "linux,initrd-end", ANDROID_INITRD_BASE + rsize))
		return "cannot move linux,initrd in /chosen";
	flush_range(dtb, dsz);
	con_str("ANDROID", "jumping");
	/* all written with caches off: drop stale instruction lines, then enter the kernel at its first instruction */
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((linux_entry_t)kdst)(dtb, 0, 0, 0);
	return "Android returned";
}
