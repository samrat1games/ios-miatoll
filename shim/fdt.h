#pragma once
#include <stdint.h>

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);

/* Overwrite a u32/u64 property of /chosen in place; returns 0 if absent or too big for a u32 cell. */
int fdt_chosen_set_u64(uintptr_t fdt, const char *prop, uint64_t v);
/* The size of the blob, 0 if there is none. */
uint32_t fdt_totalsize(uintptr_t fdt);
