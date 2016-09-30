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

#include <stdint.h>

#include "gralloc_drm.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t get_fourcc_format_for_hal_format(uint32_t hal_format);
void get_preferred_cursor_attributes(uint32_t drm_fd,
		uint32_t *cursor_width,
		uint32_t *cursor_height);

#ifdef __cplusplus
}
#endif
