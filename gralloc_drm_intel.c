/*
 * Copyright (C) 2010-2011 Chia-I Wu <olvaffe@gmail.com>
 * Copyright (C) 2010-2011 LunarG Inc.
 *
 * drm_gem_intel_copy is based on xorg-driver-intel, which has
 *
 * Copyright 1998-1999 Precision Insight, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * Copyright (c) 2005 Jesse Barnes <jbarnes@virtuousgeek.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define LOG_TAG "GRALLOC-I915"

# include <fcntl.h>
#include <cutils/log.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <drm.h>
#include <intel_bufmgr.h>
#include <i915_drm.h>
#include <drm_fourcc.h>

#include "gralloc_drm.h"
#include "gralloc_drm_priv.h"
#include "util.h"

#define DRM_CLOEXEC O_CLOEXEC
#ifndef DRM_RDWR
#define DRM_RDWR O_RDWR
#endif

struct intel_info {
	struct gralloc_drm_drv_t base;

	int fd;
	drm_intel_bufmgr *bufmgr;
	int gen;
	uint32_t cursor_width;
	uint32_t cursor_height;
};

struct intel_buffer {
	struct gralloc_drm_bo_t base;
	drm_intel_bo *ibo;
	uint32_t tiling;
};

static void calculate_aligned_geometry(uint32_t hal_format, int usage,
		uint32_t cursor_width,
		uint32_t cursor_height,
		uint32_t *width,
		uint32_t *height)
{
	uint32_t width_alignment = 1, height_alignment = 1, extra_height_div = 0;
	uint32_t fourcc_format = get_fourcc_format_for_hal_format(hal_format);
	switch(fourcc_format) {
	case DRM_FORMAT_YUV420:
		width_alignment = 32;
		height_alignment = 2;
		extra_height_div = 2;
		break;
	case DRM_FORMAT_NV16:
		width_alignment = 2;
		extra_height_div = 1;
		break;
	case DRM_FORMAT_YUYV:
		width_alignment = 2;
		break;
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV12:
		width_alignment = 2;
		height_alignment = 2;
		extra_height_div = 2;
		break;
	}

	*width = ALIGN(*width, width_alignment);
	*height = ALIGN(*height, height_alignment);

	if (extra_height_div)
		*height += *height / extra_height_div;

	if (usage & GRALLOC_USAGE_CURSOR)  {
		*width = ALIGN(*width, cursor_width);
		*height = ALIGN(*height, cursor_height);
	} else if (usage & GRALLOC_USAGE_HW_FB) {
		*width = ALIGN(*width, 64);
	} else if (usage & GRALLOC_USAGE_HW_TEXTURE) {
		/* see 2D texture layout of DRI drivers */
		*width = ALIGN(*width, 4);
		*height = ALIGN(*height, 2);
	}

	if (fourcc_format == DRM_FORMAT_YUV420)
		*width = ALIGN(*width, 128);
}

static void calculate_aligned_offsets(struct gralloc_drm_bo_t *bo,
		uint32_t height,
		uint32_t *pitches,
		uint32_t *offsets,
		uint32_t *handles)
{
	/*
	 * TODO - should take account hw specific padding, alignment
	 * for camera, video decoder etc.
	 */

	struct intel_buffer *ib = (struct intel_buffer *) bo;

	memset(pitches, 0, 4 * sizeof(uint32_t));
	memset(offsets, 0, 4 * sizeof(uint32_t));
	memset(handles, 0, 4 * sizeof(uint32_t));

	pitches[0] = ib->base.handle->stride;
	handles[0] = ib->base.fb_handle;

	switch(ib->base.handle->format) {
		case HAL_PIXEL_FORMAT_YV12:

			// U and V stride are half of Y plane
			pitches[2] = ALIGN(pitches[0] / 2, 16);
			pitches[1] = ALIGN(pitches[0] / 2, 16);

			// like I420 but U and V are in reverse order
			offsets[2] = offsets[0] +
				pitches[0] * height;
			offsets[1] = offsets[2] +
				pitches[2] * height/2;

			handles[1] = handles[2] = handles[0];
			break;
	}
}

static void intel_resolve_format(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo,
		uint32_t *pitches, uint32_t *offsets, uint32_t *handles)
{
    struct intel_info *info = (struct intel_info *) drv;
    uint32_t aligned_width = bo->handle->width;
    uint32_t aligned_height = bo->handle->height;
    calculate_aligned_geometry(bo->handle->format, bo->handle->usage,
			       info->cursor_width, info->cursor_height,
			       &aligned_width, &aligned_height);

    calculate_aligned_offsets(bo, aligned_height,
			      pitches, offsets, handles);
}

static drm_intel_bo *alloc_ibo(struct intel_info *info,
		const struct gralloc_drm_handle_t *handle,
		uint32_t *tiling, unsigned long *stride)
{
	drm_intel_bo *ibo;
	const char *name;
	uint32_t aligned_width, aligned_height, bpp;
	unsigned long flags;

	flags = 0;
	bpp = gralloc_drm_get_bpp(handle->format);
	if (!bpp) {
		ALOGE("unrecognized format 0x%x", handle->format);
		return NULL;
	}

	aligned_width = handle->width;
	aligned_height = handle->height;
	calculate_aligned_geometry(handle->format, handle->usage,
				   info->cursor_width, info->cursor_height,
				   &aligned_width, &aligned_height);
	if (handle->usage & GRALLOC_USAGE_HW_FB || handle->usage & GRALLOC_USAGE_CURSOR) {
		unsigned long max_stride;

		max_stride = 32 * 1024;
		if (info->gen < 50)
			max_stride /= 2;
		if (info->gen < 40)
			max_stride /= 2;
		if (handle->usage & GRALLOC_USAGE_CURSOR)  {
		    *tiling = I915_TILING_NONE;
		    name = "gralloc-cursor";
		} else {
		    name = "gralloc-fb";
		    *tiling = I915_TILING_X;
		}

		flags = BO_ALLOC_FOR_RENDER;
		*stride = aligned_width * bpp;
		if (*stride > max_stride) {
			*tiling = I915_TILING_NONE;
			max_stride = 32 * 1024;
			if (*stride > max_stride)
				return NULL;
		}

		while (1) {
			ibo = drm_intel_bo_alloc_tiled(info->bufmgr, name,
					aligned_width, aligned_height,
					bpp, tiling, stride, flags);
			if (!ibo || *stride > max_stride) {
				if (ibo) {
					drm_intel_bo_unreference(ibo);
					ibo = NULL;
				}

				if (*tiling != I915_TILING_NONE) {
					/* retry */
					*tiling = I915_TILING_NONE;
					max_stride = 32 * 1024;
					continue;
				}
			}
			if (ibo)
				drm_intel_bo_disable_reuse(ibo);
			break;
		}
	}
	else {
		if (handle->usage & GRALLOC_USAGE_HW_TEXTURE) {
			name = "gralloc-texture";
		}
		else {
			name = "gralloc-buffer";
		}

		if (get_fourcc_format_for_hal_format(handle->format) == DRM_FORMAT_YUV420) {
			*tiling = I915_TILING_NONE;
			name = "gralloc-videotexture";
		} else {
			if (handle->usage & (GRALLOC_USAGE_SW_READ_OFTEN |
						GRALLOC_USAGE_SW_WRITE_OFTEN))
				*tiling = I915_TILING_NONE;
			else if ((handle->usage & GRALLOC_USAGE_HW_RENDER) ||
				 ((handle->usage & GRALLOC_USAGE_HW_TEXTURE) &&
				  handle->width >= 64))
				*tiling = I915_TILING_X;
			else
				*tiling = I915_TILING_NONE;
		}

		if (handle->usage & GRALLOC_USAGE_HW_RENDER)
			flags = BO_ALLOC_FOR_RENDER;

		ibo = drm_intel_bo_alloc_tiled(info->bufmgr, name,
				aligned_width, aligned_height,
				bpp, tiling, stride, flags);
	}

	return ibo;
}

static struct gralloc_drm_bo_t *intel_alloc(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_handle_t *handle)
{
	struct intel_info *info = (struct intel_info *) drv;
	struct intel_buffer *ib;

	ib = calloc(1, sizeof(*ib));
	if (!ib)
		return NULL;
#ifdef USE_NAME
        if (handle->name) {
#else
        if (handle->prime_fd >= 0) {
#endif
		uint32_t dummy;
#ifdef USE_NAME
                ib->ibo = drm_intel_bo_gem_create_from_name(info->bufmgr,
                                "gralloc-r", handle->name)
#else
                ib->ibo = drm_intel_bo_gem_create_from_prime(info->bufmgr,
				handle->prime_fd, 0);
#endif
		if (!ib->ibo) {
                        ALOGE("failed to create ibo from prime_fd %d",
                                        handle->prime_fd);
			free(ib);
			return NULL;
		}

		if (drm_intel_bo_get_tiling(ib->ibo, &ib->tiling, &dummy)) {
			ALOGE("failed to get ibo tiling");
			drm_intel_bo_unreference(ib->ibo);
			free(ib);
			return NULL;
		}
	}
	else {
		unsigned long stride;

		ib->ibo = alloc_ibo(info, handle, &ib->tiling, &stride);
		if (!ib->ibo) {
			ALOGE("failed to allocate ibo %dx%d (format %d)",
					handle->width,
					handle->height,
					handle->format);
			free(ib);
			return NULL;
		}

                handle->stride = stride;
#ifdef USE_NAME
                int r = drm_intel_bo_flink(ib->ibo, (uint32_t *) &handle->name));
#else

                int r = drmPrimeHandleToFD(info->fd,
                                           ib->ibo->handle,
                                           DRM_CLOEXEC | DRM_RDWR,
                                           &handle->prime_fd);
#endif
                if (r < 0) {
                    ALOGE("cannot get prime-fd for handle");
		    drm_intel_bo_unreference(ib->ibo);
		    free(ib);
		    return NULL;
		}
	}

	ib->base.fb_handle = ib->ibo->handle;

	ib->base.handle = handle;

	return &ib->base;
}

static void intel_free(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo)
{
	struct intel_buffer *ib = (struct intel_buffer *) bo;

	drm_intel_bo_unreference(ib->ibo);
	free(ib);
}

static int intel_map(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo,
		int x, int y, int w, int h,
		int enable_write, void **addr)
{
	struct intel_buffer *ib = (struct intel_buffer *) bo;
	int err;

	if (ib->tiling != I915_TILING_NONE ||
	    (ib->base.handle->usage & GRALLOC_USAGE_HW_FB))
		err = drm_intel_gem_bo_map_gtt(ib->ibo);
	else
		err = drm_intel_bo_map(ib->ibo, enable_write);
	if (!err)
		*addr = ib->ibo->virtual;

	return err;
}

static void intel_unmap(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo)
{
	struct intel_buffer *ib = (struct intel_buffer *) bo;

	if (ib->tiling != I915_TILING_NONE ||
	    (ib->base.handle->usage & GRALLOC_USAGE_HW_FB))
		drm_intel_gem_bo_unmap_gtt(ib->ibo);
	else
		drm_intel_bo_unmap(ib->ibo);
}

#include "intel_chipset.h" /* for platform detection macros */
static void gen_init(struct intel_info *info)
{
	struct drm_i915_getparam gp;
	int id;

	memset(&gp, 0, sizeof(gp));
	gp.param = I915_PARAM_CHIPSET_ID;
	gp.value = &id;
	if (drmCommandWriteRead(info->fd, DRM_I915_GETPARAM, &gp, sizeof(gp)))
		id = 0;

	/* GEN4, G4X, GEN5, GEN6, GEN7 */
	if ((IS_9XX(id) || IS_G4X(id)) && !IS_GEN3(id)) {
		if (IS_GEN7(id))
			info->gen = 70;
		else if (IS_GEN6(id))
			info->gen = 60;
		else if (IS_GEN5(id))
			info->gen = 50;
		else
			info->gen = 40;
	}
	else {
		info->gen = 30;
	}

	get_preferred_cursor_attributes(info->fd,
					&info->cursor_width,
					&info->cursor_height);
}

static void intel_destroy(struct gralloc_drm_drv_t *drv)
{
	struct intel_info *info = (struct intel_info *) drv;

	drm_intel_bufmgr_destroy(info->bufmgr);
	free(info);
}

struct gralloc_drm_drv_t *gralloc_drm_drv_create_for_intel(int fd)
{
	struct intel_info *info;

	info = calloc(1, sizeof(*info));
	if (!info) {
		ALOGE("failed to allocate driver info");
		return NULL;
	}

	info->fd = fd;
	info->bufmgr = drm_intel_bufmgr_gem_init(info->fd, 16 * 1024);
	if (!info->bufmgr) {
		ALOGE("failed to create buffer manager");
		free(info);
		return NULL;
	}

	gen_init(info);

	info->base.destroy = intel_destroy;
	info->base.alloc = intel_alloc;
	info->base.free = intel_free;
	info->base.map = intel_map;
	info->base.unmap = intel_unmap;
	info->base.resolve_format = intel_resolve_format;

	return &info->base;
}
