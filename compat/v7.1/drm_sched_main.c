/*
 * Copyright (c) 2026
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <drm/gpu_scheduler.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 1, 0)
#endif
