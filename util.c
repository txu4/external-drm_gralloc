/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "GRALLOC-UTIL"

#include <cutils/properties.h>
#include <cutils/log.h>
#include <system/graphics.h>

#include <drm_fourcc.h>
#include <xf86drm.h>

#include "util.h"

#if !defined(DRM_CAP_CURSOR_WIDTH)
#define DRM_CAP_CURSOR_WIDTH 0x8
#endif

#if !defined(DRM_CAP_CURSOR_HEIGHT)
#define DRM_CAP_CURSOR_HEIGHT 0x9
#endif

static const uint32_t kDefaultCursorWidth = 64;
static const uint32_t kDefaultCursorHeight = 64;

uint32_t get_fourcc_format_for_hal_format(uint32_t hal_format) {
	switch (hal_format) {
	case HAL_PIXEL_FORMAT_RGBA_8888:
		return DRM_FORMAT_ABGR8888;
	case HAL_PIXEL_FORMAT_RGBX_8888:
		return DRM_FORMAT_XBGR8888;
	case HAL_PIXEL_FORMAT_RGB_888:
		return DRM_FORMAT_BGR888;
	case HAL_PIXEL_FORMAT_BGRA_8888:
		return DRM_FORMAT_ARGB8888;
	case HAL_PIXEL_FORMAT_RGB_565:
		return DRM_FORMAT_RGB565;
	case HAL_PIXEL_FORMAT_YV12:
	case HAL_PIXEL_FORMAT_YCbCr_420_888:
		return DRM_FORMAT_YUV420;
	case HAL_PIXEL_FORMAT_YCbCr_422_I:
		return DRM_FORMAT_YUYV;
	case HAL_PIXEL_FORMAT_YCbCr_422_SP:
		return DRM_FORMAT_NV16;
	case HAL_PIXEL_FORMAT_YCrCb_420_SP:
		return DRM_FORMAT_NV21;
	case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
		return DRM_FORMAT_XBGR8888;
	default:
		ALOGI("Unknown HAL Format 0x%x", hal_format);
		return 0;
	}
}

void get_preferred_cursor_attributes(uint32_t drm_fd,
				     uint32_t *cursor_width,
				     uint32_t *cursor_height)
{
	uint64_t width = 0, height = 0;
	if (drmGetCap(drm_fd, DRM_CAP_CURSOR_WIDTH, &width)) {
		ALOGE("cannot get cursor width.");
	} else if (drmGetCap(drm_fd, DRM_CAP_CURSOR_HEIGHT, &height)) {
		ALOGE("cannot get cursor height.");
	}

	if (!width)
		width = kDefaultCursorWidth;

	*cursor_width = width;

	if (!height)
		height = kDefaultCursorHeight;

	*cursor_height = height;
}
