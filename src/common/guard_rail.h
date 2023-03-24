/*
 *    This file is part of darktable,
 *    Copyright (C) 2023 darktable developers.
 *
 *    darktable is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    darktable is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "dttypes.h"
#include "math.h"


void compensate_negative_RGB_at_constant_luminance(const dt_aligned_pixel_t luminance_coeffs,
                                                   const dt_aligned_pixel_t RGB_in,
                                                   dt_aligned_pixel_t RGB_out)
{
  // Handling of the lower gamut boundary (negative components)
  // Calculate the original luminance. We will restore this later.
  const float original_luminance = scalar_product(RGB_in, luminance_coeffs);
  const float min_RGB = MIN(MIN(RGB_in[0], RGB_in[1]), RGB_in[2]);
  const float offset = MAX(-min_RGB, 0.f);
  dt_aligned_pixel_t RGB_offset;
  // Offset to get rid of negatives. This will increase the luminance.
  for_each_channel(c) RGB_offset[c] = RGB_in[c] + offset;
  // Restore the luminance to the original
  const float offset_luminance = scalar_product(RGB_offset, luminance_coeffs);
  const float gain = offset_luminance != 0.f ? original_luminance / offset_luminance : 1.f;
  for_each_channel(c) RGB_out[c] = RGB_offset[c] * gain;
}

void compensate_high_RGB_values(const float white_target, const float t,
                                const dt_aligned_pixel_t luminance_coeffs,
                                const dt_aligned_pixel_t RGB_in, dt_aligned_pixel_t RGB_out)
{
  // High side correction - bring values above domain upper boundary
  // down to the limits.
  // If max_RGB is below white_target, we don't need to do anything.
  // However, the calculation is still applied to every pixel uniformly.
  // It is just a no-op for those already within the closed domain.
  const float max_RGB = MAX(MAX(RGB_in[0], RGB_in[1]), RGB_in[2]);

  // 1. decompose the input RGB into luminance and chrominance parts
  // The luminance part can be though of as an achromatic RGB triplet
  // with the same luminance as RGB_in.
  const float luminance = scalar_product(RGB_in, luminance_coeffs);

  const float relative_luminance = max_RGB / white_target;
  const float chrominance_coeff = relative_luminance > white_target ? white_target / relative_luminance : 1.f;

  dt_aligned_pixel_t RGB_adjusted;
  for_each_channel(c) RGB_adjusted[c] = luminance + chrominance_coeff * (RGB_in[c] - luminance);
  const float max_RGB_adjusted = MAX(MAX(RGB_adjusted[0], RGB_adjusted[1]), RGB_adjusted[2]);
  const float scale = max_RGB_adjusted > white_target ? white_target / max_RGB_adjusted : 1.f;

  for_each_channel(c) RGB_out[c] = scale * RGB_adjusted[c];
}
