#pragma once
#include <stdint.h>

/* Boot the SWEETXNU payload found in the ABL ramdisk; returns only on error. */
const char *loader_boot(uintptr_t dtb, const char *extra);

/*
 * Start the Android kernel that tools/mkdual.py packed behind the xnu payload (dual boot); returns only on error.
 * The kernel and its ramdisk are copied to free RAM, /chosen/linux,initrd-* are pointed at the new ramdisk and the
 * kernel is entered the way the arm64 boot protocol wants it: x0 = the device tree, MMU and caches off.
 */
const char *android_boot(uintptr_t dtb);
