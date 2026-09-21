/*
 * Copyright (c) 2026
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/slab.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 1, 0)
unsigned int __bitmap_weighted_or(unsigned long *dst, const unsigned long *bitmap1,
				  const unsigned long *bitmap2, unsigned int bits);
unsigned int __bitmap_weighted_xor(unsigned long *dst, const unsigned long *bitmap1,
				  const unsigned long *bitmap2, unsigned int bits);

#define BITMAP_WEIGHT(FETCH, bits)	\
({										\
	unsigned int __bits = (bits), idx, w = 0;				\
										\
	for (idx = 0; idx < __bits / BITS_PER_LONG; idx++)			\
		w += hweight_long(FETCH);					\
										\
	if (__bits % BITS_PER_LONG)						\
		w += hweight_long((FETCH) & BITMAP_LAST_WORD_MASK(__bits));	\
										\
	w;									\
})

unsigned int __bitmap_weighted_or(unsigned long *dst, const unsigned long *bitmap1,
				  const unsigned long *bitmap2, unsigned int bits)
{
	return BITMAP_WEIGHT(({dst[idx] = bitmap1[idx] | bitmap2[idx]; dst[idx]; }), bits);
}
EXPORT_SYMBOL(__bitmap_weighted_or);

unsigned int __bitmap_weighted_xor(unsigned long *dst, const unsigned long *bitmap1,
				  const unsigned long *bitmap2, unsigned int bits)
{
	return BITMAP_WEIGHT(({dst[idx] = bitmap1[idx] ^ bitmap2[idx]; dst[idx]; }), bits);
}
EXPORT_SYMBOL(__bitmap_weighted_xor);
#endif