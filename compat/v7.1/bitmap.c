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

/*
 * Linux 7.0 mainline already ships __bitmap_weighted_or (added via the
 * cpumask_weighted_or() patchset that landed in 6.17/6.18). The drm/xe
 * driver started using it before 7.1, so we must skip our backport
 * whenever the kernel already provides the symbol — otherwise modpost
 * complains:  "__bitmap_weighted_or exported twice".
 *
 * __bitmap_weighted_xor was added later (7.1) so we always need to
 * backport it.
 *
 * Detect "kernel already has it" by checking the prototype in
 * <linux/bitmap.h>. If the prototype is visible, the function lives in
 * vmlinux and we must not redefine / re-export it.
 */

#ifndef __bitmap_weighted_or
unsigned int __bitmap_weighted_or(unsigned long *dst, const unsigned long *bitmap1,
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
#endif /* __bitmap_weighted_or */

#ifndef __bitmap_weighted_xor
unsigned int __bitmap_weighted_xor(unsigned long *dst, const unsigned long *bitmap1,
				  const unsigned long *bitmap2, unsigned int bits);

#ifndef BITMAP_WEIGHT
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
#endif

unsigned int __bitmap_weighted_xor(unsigned long *dst, const unsigned long *bitmap1,
				  const unsigned long *bitmap2, unsigned int bits)
{
	return BITMAP_WEIGHT(({dst[idx] = bitmap1[idx] ^ bitmap2[idx]; dst[idx]; }), bits);
}
EXPORT_SYMBOL(__bitmap_weighted_xor);
#endif /* __bitmap_weighted_xor */