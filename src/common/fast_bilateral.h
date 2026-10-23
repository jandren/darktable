/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Edge-aware surface blur of a single-channel image, using the
 * permutohedral-lattice bilateral filter from iop/Permutohedral.h (same
 * implementation as iop/bilateral.cc), so C callers don't need to link
 * against the C++ template directly.
 *
 * The filter has no notion of linear vs. log space: callers wanting a
 * log-space blur (as fast_surface_blur() and fast_eigf_surface_blur() also
 * support) should convert before and after, same as they do for those.
 *
 * @param image    In/out single-channel buffer, width * height floats.
 * @param width    Image width.
 * @param height   Image height.
 * @param sigma_s  Spatial std-dev of the blur, in pixels.
 * @param sigma_r  Range std-dev of the blur, in the same units as image[].
 */
void dt_bilateral_surface_blur(float *const image,
                               const size_t width,
                               const size_t height,
                               const float sigma_s,
                               const float sigma_r);

#ifdef __cplusplus
}
#endif
