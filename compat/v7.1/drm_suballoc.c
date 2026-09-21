/*
 * Copyright (c) 2026
 *
 * Backport functionality for older kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/drm_suballoc.h>
#include <drm/drm_print.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/dma-fence.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 1, 0)

static void drm_suballoc_remove_locked(struct drm_suballoc *sa);

static void drm_suballoc_try_free(struct drm_suballoc_manager *sa_manager);

static void drm_suballoc_remove_locked(struct drm_suballoc *sa)
{
	struct drm_suballoc_manager *sa_manager = sa->manager;

	if (sa_manager->hole == &sa->olist)
		sa_manager->hole = sa->olist.prev;
	list_del_init(&sa->olist);
	list_del_init(&sa->flist);
	dma_fence_put(sa->fence);
	kfree(sa);
}

static void drm_suballoc_try_free(struct drm_suballoc_manager *sa_manager)
{
	struct drm_suballoc *sa, *tmp;

	if (sa_manager->hole->next == &sa_manager->olist)
		return;
	sa = list_entry(sa_manager->hole->next, struct drm_suballoc, olist);
	list_for_each_entry_safe_from(sa, tmp, &sa_manager->olist, olist) {
		if (!sa->fence || !dma_fence_is_signaled(sa->fence))
			return;
		drm_suballoc_remove_locked(sa);
	}
}

static size_t drm_suballoc_hole_soffset(struct drm_suballoc_manager *sa_manager)
{
	struct list_head *hole = sa_manager->hole;

	if (hole != &sa_manager->olist)
		return list_entry(hole, struct drm_suballoc, olist)->eoffset;
	return 0;
}

static size_t drm_suballoc_hole_eoffset(struct drm_suballoc_manager *sa_manager)
{
	struct list_head *hole = sa_manager->hole;

	if (hole->next != &sa_manager->olist)
		return list_entry(hole->next, struct drm_suballoc, olist)->soffset;
	return sa_manager->size;
}

static bool drm_suballoc_try_alloc(struct drm_suballoc_manager *sa_manager,
				   struct drm_suballoc *sa,
				   size_t size, size_t align)
{
	size_t soffset, eoffset, wasted;

	soffset = drm_suballoc_hole_soffset(sa_manager);
	eoffset = drm_suballoc_hole_eoffset(sa_manager);
	wasted = round_up(soffset, align) - soffset;
	if ((eoffset - soffset) >= (size + wasted)) {
		soffset += wasted;
		sa->manager = sa_manager;
		sa->soffset = soffset;
		sa->eoffset = soffset + size;
		list_add(&sa->olist, sa_manager->hole);
		INIT_LIST_HEAD(&sa->flist);
		sa_manager->hole = &sa->olist;
		return true;
	}
	return false;
}

static bool __drm_suballoc_event(struct drm_suballoc_manager *sa_manager,
				 size_t size, size_t align)
{
	size_t soffset, eoffset, wasted;
	unsigned int i;

	for (i = 0; i < DRM_SUBALLOC_MAX_QUEUES; ++i)
		if (!list_empty(&sa_manager->flist[i]))
			return true;
	soffset = drm_suballoc_hole_soffset(sa_manager);
	eoffset = drm_suballoc_hole_eoffset(sa_manager);
	wasted = round_up(soffset, align) - soffset;
	return ((eoffset - soffset) >= (size + wasted));
}

/**
 * drm_suballoc_event() - Check if we can stop waiting
 * @sa_manager: pointer to the sa_manager
 * @size: number of bytes we want to allocate
 * @align: alignment we need to match
 *
 * Return: true if either there is a fence we can wait for or
 * enough free memory to satisfy the allocation directly.
 * false otherwise.
 */
static bool drm_suballoc_event(struct drm_suballoc_manager *sa_manager,
			       size_t size, size_t align)
{
	bool ret;

	spin_lock(&sa_manager->wq.lock);
	ret = __drm_suballoc_event(sa_manager, size, align);
	spin_unlock(&sa_manager->wq.lock);
	return ret;
}

static bool drm_suballoc_next_hole(struct drm_suballoc_manager *sa_manager,
				   struct dma_fence **fences,
				   unsigned int *tries)
{
	struct drm_suballoc *best_bo = NULL;
	unsigned int i, best_idx;
	size_t soffset, best, tmp;

	/* if hole points to the end of the buffer */
	if (sa_manager->hole->next == &sa_manager->olist) {
		/* try again with its beginning */
		sa_manager->hole = &sa_manager->olist;
		return true;
	}
	soffset = drm_suballoc_hole_soffset(sa_manager);
	/* to handle wrap around we add sa_manager->size */
	best = sa_manager->size * 2;
	/* go over all fence list and try to find the closest sa
	 * of the current last
	 */
	for (i = 0; i < DRM_SUBALLOC_MAX_QUEUES; ++i) {
		struct drm_suballoc *sa;

		fences[i] = NULL;
		if (list_empty(&sa_manager->flist[i]))
			continue;
		sa = list_first_entry(&sa_manager->flist[i],
				      struct drm_suballoc, flist);
		if (!dma_fence_is_signaled(sa->fence)) {
			fences[i] = sa->fence;
			continue;
		}
		/* limit the number of tries each freelist gets */
		if (tries[i] > 2)
			continue;
		tmp = sa->soffset;
		if (tmp < soffset) {
			/* wrap around, pretend it's after */
			tmp += sa_manager->size;
		}
		tmp -= soffset;
		if (tmp < best) {
			/* this sa bo is the closest one */
			best = tmp;
			best_idx = i;
			best_bo = sa;
		}
	}
	if (best_bo) {
		++tries[best_idx];
		sa_manager->hole = best_bo->olist.prev;
		/*
		 * We know that this one is signaled,
		 * so it's safe to remove it.
		 */
		drm_suballoc_remove_locked(best_bo);
		return true;
	}
	return false;
}

/**
 * drm_suballoc_alloc() - Allocate uninitialized suballoc object.
 * @gfp: gfp flags used for memory allocation.
 *
 * Allocate memory for an uninitialized suballoc object. Intended usage is
 * allocate memory for suballoc object outside of a reclaim tainted context
 * and then be initialized at a later time in a reclaim tainted context.
 *
 * @drm_suballoc_free() should be used to release the memory if returned
 * suballoc object is in uninitialized state.
 *
 * Return: a new uninitialized suballoc object, or an ERR_PTR(-ENOMEM).
 */
struct drm_suballoc *drm_suballoc_alloc(gfp_t gfp)
{
	struct drm_suballoc *sa;

	sa = kmalloc_obj(*sa, gfp);
	if (!sa)
		return ERR_PTR(-ENOMEM);
	sa->manager = NULL;
	return sa;
}
EXPORT_SYMBOL(drm_suballoc_alloc);

/**
 * drm_suballoc_insert() - Initialize a suballocation and insert a hole.
 * @sa_manager: pointer to the sa_manager
 * @sa: The struct drm_suballoc.
 * @size: number of bytes we want to suballocate.
 * @intr: Whether to perform waits interruptible. This should typically
 *        always be true, unless the caller needs to propagate a
 *        non-interruptible context from above layers.
 * @align: Alignment. Must not exceed the default manager alignment.
 *         If @align is zero, then the manager alignment is used.
 *
 * Try to make a suballocation on a pre-allocated suballoc object of size @size,
 * which will be rounded up to the alignment specified in specified in
 * drm_suballoc_manager_init().
 *
 * Return: zero on success, errno on failure.
 */
int drm_suballoc_insert(struct drm_suballoc_manager *sa_manager,
			struct drm_suballoc *sa, size_t size, bool intr,
			size_t align)
{
	struct dma_fence *fences[DRM_SUBALLOC_MAX_QUEUES];
	unsigned int tries[DRM_SUBALLOC_MAX_QUEUES];
	unsigned int count;
	int i, r;

	if (WARN_ON_ONCE(align > sa_manager->align))
		return -EINVAL;
	if (WARN_ON_ONCE(size > sa_manager->size || !size))
		return -EINVAL;
	if (!align)
		align = sa_manager->align;
	sa->manager = sa_manager;
	sa->fence = NULL;
	INIT_LIST_HEAD(&sa->olist);
	INIT_LIST_HEAD(&sa->flist);
	spin_lock(&sa_manager->wq.lock);
	do {
		for (i = 0; i < DRM_SUBALLOC_MAX_QUEUES; ++i)
			tries[i] = 0;
		do {
			drm_suballoc_try_free(sa_manager);
			if (drm_suballoc_try_alloc(sa_manager, sa, size,
						   align)) {
				spin_unlock(&sa_manager->wq.lock);
				return 0;
			}
			/* see if we can skip over some allocations */
		} while (drm_suballoc_next_hole(sa_manager, fences, tries));
		for (i = 0, count = 0; i < DRM_SUBALLOC_MAX_QUEUES; ++i)
			if (fences[i])
				fences[count++] = dma_fence_get(fences[i]);
		if (count) {
			long t;

			spin_unlock(&sa_manager->wq.lock);
			t = dma_fence_wait_any_timeout(fences, count, intr,
						       MAX_SCHEDULE_TIMEOUT,
						       NULL);
			for (i = 0; i < count; ++i)
				dma_fence_put(fences[i]);
			r = (t > 0) ? 0 : t;
			spin_lock(&sa_manager->wq.lock);
		} else if (intr) {
			/* if we have nothing to wait for block */
			r = wait_event_interruptible_locked(
				sa_manager->wq,
				__drm_suballoc_event(sa_manager, size, align));
		} else {
			spin_unlock(&sa_manager->wq.lock);
			wait_event(sa_manager->wq,
				   drm_suballoc_event(sa_manager, size, align));
			r = 0;
			spin_lock(&sa_manager->wq.lock);
		}
	} while (!r);
	spin_unlock(&sa_manager->wq.lock);
	sa->manager = NULL;
	return r;
}
EXPORT_SYMBOL(drm_suballoc_insert);

#endif