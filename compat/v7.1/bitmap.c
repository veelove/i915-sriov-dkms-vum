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
#include <linux/version.h>

/*
 * __bitmap_weighted_or was added to the upstream kernel in 6.18 (via the
 * cpumask_weighted_or() patchset), and __bitmap_weighted_xor in 7.0.  When
 * the kernel already provides the symbol, modpost rejects our backport
 * with "__bitmap_weighted_or exported twice".
 *
 * Use LINUX_VERSION_CODE guards so each symbol is only defined on kernels
 * that lack it.
 */

#define COMPAT_BITMAP_WEIGHT(FETCH, bits)				\
({									\
	unsigned int __bits = (bits), idx, w = 0;			\
									\
	for (idx = 0; idx < __bits / BITS_PER_LONG; idx++)		\
		w += hweight_long(FETCH);				\
									\
	if (__bits % BITS_PER_LONG)					\
		w += hweight_long((FETCH) & BITMAP_LAST_WORD_MASK(__bits)); \
									\
	w;								\
})

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
unsigned int __bitmap_weighted_or(unsigned long *dst, const unsigned long *bitmap1,
				  const unsigned long *bitmap2, unsigned int bits)
{
	return COMPAT_BITMAP_WEIGHT(({dst[idx] = bitmap1[idx] | bitmap2[idx]; dst[idx]; }), bits);
}
EXPORT_SYMBOL(__bitmap_weighted_or);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
unsigned int __bitmap_weighted_xor(unsigned long *dst, const unsigned long *bitmap1,
				  const unsigned long *bitmap2, unsigned int bits)
{
	return COMPAT_BITMAP_WEIGHT(({dst[idx] = bitmap1[idx] ^ bitmap2[idx]; dst[idx]; }), bits);
}
EXPORT_SYMBOL(__bitmap_weighted_xor);
#endif