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

// Single-channel bilateral surface blur, built on the same permutohedral
// lattice (Adams, Baek, Davis) used by iop/bilateral.cc, exposed as a
// plain C function so it can be called from C translation units.

#include <math.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "common/darktable.h"
#include "common/fast_bilateral.h"
#include "iop/Permutohedral.h"

void dt_bilateral_surface_blur(float *const image,
                               const size_t width,
                               const size_t height,
                               const float sigma_s,
                               const float sigma_r)
{
  const size_t npixels = width * height;
  const float inv_sigma_s = 1.0f / fmaxf(sigma_s, 1.0f);
  const float inv_sigma_r = 1.0f / fmaxf(sigma_r, 1e-6f);

  PermutohedralLattice<3, 2> lattice(npixels, dt_get_num_threads());

  // splat into the lattice: 2D position + the pixel's own value as the range dimension
  DT_OMP_FOR(shared(lattice))
  for(size_t j = 0; j < height; j++)
  {
    const int thread = dt_get_thread_num();
    size_t index = j * width;
    for(size_t i = 0; i < width; i++, index++)
    {
      const float value = image[index];
      float pos[3] = { i * inv_sigma_s, j * inv_sigma_s, value * inv_sigma_r };
      float val[2] = { value, 1.0f };
      lattice.splat(pos, val, index, thread);
    }
  }

  lattice.merge_splat_threads();
  lattice.blur();

  DT_OMP_FOR(shared(lattice))
  for(size_t index = 0; index < npixels; index++)
  {
    float val[2];
    lattice.slice(val, index);
    image[index] = val[0] / val[1];
  }
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
