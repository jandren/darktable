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

/*
DOCUMENTATION
This module implements a scene-referred local contrast enhancement algorithm,
designed to enhance local details while preserving edges and avoiding artifacts.

It builds upon the original proof-of-concept algorithm proposed by WileCoyote:
https://discuss.pixls.us/t/experiments-with-a-scene-referred-local-contrast-module-proof-of-concept/55402

And then further explored and optimized by Christian Bouhon
https://discuss.pixls.us/t/contrast-management-rgb-a-new-scene-referred-approach-poc/56004

Current status as implemented by Jandren:
- Local contrast in log space based on the eigf surface blur filter.
*/



#include "common/extra_optimizations.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bauhaus/bauhaus.h"
#include "common/darktable.h"
#include "common/fast_guided_filter.h"
#include "common/eigf.h"
#include "common/luminance_mask.h"
#include "control/conf.h"
#include "control/control.h"
#include "develop/blend.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "develop/imageop_math.h"
#include "develop/imageop_gui.h"
#include "gui/accelerators.h"
#include "gui/draw.h"
#include "dtgtk/paint.h"
#include "dtgtk/togglebutton.h"
#include "dtgtk/expander.h"
#include "gui/gtk.h"
#include "gui/presets.h"
#include "iop/iop_api.h"
#include "common/iop_group.h"

#ifdef _OPENMP
#include <omp.h>
#endif

DT_MODULE_INTROSPECTION(2, dt_iop_contrastntexture_params_t)

typedef struct dt_iop_contrastntexture_params_t
{
  float gain_coarse_details;  // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "coarse details"
  float gain_broad_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "broad details"
  float gain_medium_details;  // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "medium details"
  float gain_fine_details;    // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "fine details"
  float gain_micro_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "micro details"
  float gain_micro1_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "micro+1 details"
  float gain_micro2_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "micro+2 details"
  float gain_micro3_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "micro+3 details"
  float gain_micro4_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "micro+4 details"
  float gain_micro5_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "micro+5 details"
  float gain_micro6_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "micro+6 details"
  float gain_micro7_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "micro+7 details"
  float gain_micro8_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "micro+8 details"
  float gain_micro9_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "micro+9 details"
  float gain_micro10_details;   // $MIN: -1.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "micro+10 details" 
  float detail_level;         // $MIN: 1.0 $MAX: 15.0 $DEFAULT: 4.0 $DESCRIPTION: "base detail level"
  float edge_protection;      // $MIN: -10.0 $MAX: 10.0 $DEFAULT: 0.0 $DESCRIPTION: "adjust edge protection"
  int filter_iterations;      // $MIN: 1 $MAX: 20 $DEFAULT: 1 $DESCRIPTION: "filter iterations"
  float noise_bias;           // $MIN: 0.0 $MAX: 1.0 $DEFAULT: 0.001 $DESCRIPTION: "noise bias"
  float gain_shadows;         // $MIN: -5.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "shadows"
  float gain_highlights;      // $MIN: -5.0 $MAX: 5.0 $DEFAULT: 0.0 $DESCRIPTION: "highlights"
} dt_iop_contrastntexture_params_t;

typedef enum dt_iop_contrastntexture_details_display_t
{
  DT_LC_MASK_OFF = -1,
  DT_LC_MASK_COARSE = 0,
  DT_LC_MASK_BROAD = 1,
  DT_LC_MASK_MEDIUM = 2,
  DT_LC_MASK_FINE = 3,
  DT_LC_MASK_MICRO = 4,
  DT_LC_MASK_MICRO1 = 5,
  DT_LC_MASK_MICRO2 = 6,
  DT_LC_MASK_MICRO3 = 7,
  DT_LC_MASK_MICRO4 = 8,
  DT_LC_MASK_MICRO5 = 9,
  DT_LC_MASK_MICRO6 = 10,
  DT_LC_MASK_MICRO7 = 11,
  DT_LC_MASK_MICRO8 = 12,
  DT_LC_MASK_MICRO9 = 13,
  DT_LC_MASK_MICRO10 = 14,
  DT_LC_MASK_LAST = 15
} dt_iop_contrastntexture_details_display_t;

typedef struct dt_iop_contrastntexture_data_t
{
  // coarse, broad, medium, fine, micro
  float gain_details[DT_LC_MASK_LAST]; 
  int radius_details[DT_LC_MASK_LAST];
  float scale_details[DT_LC_MASK_LAST];
  size_t max_level;
  float feathering;
  int iterations;
  float noise_bias;
  float slope_shadows;
  float slope_highlights;
  float midtones_width;
  float midtones_polynomial[3];
} dt_iop_contrastntexture_data_t;

typedef struct dt_iop_contrastntexture_gui_data_t
{
  // Flags
  dt_iop_contrastntexture_details_display_t details_display;

  // GTK widgets adjustments
  GtkWidget *gain_details[DT_LC_MASK_LAST]; // coarse, broad, medium, fine, micro
  GtkWidget *gain_shadows;
  GtkWidget *gain_highlights;

  // GTK widgets filter settings
  GtkWidget *detail_level;
  GtkWidget *edge_protection;
  GtkWidget *filter_iterations;
  GtkWidget *noise_bias;
} dt_iop_contrastntexture_gui_data_t;


const char *name()
{
  return _("contrast & texture");
}

const char *aliases()
{
  return _("local contrast|texture|clarity|detail enhancement");
}

const char **description(dt_iop_module_t *self)
{
  return dt_iop_set_description
    (self, _("enhance local contrast by boosting details while preserving edges"),
     _("creative"),
     _("linear, RGB, scene-referred"),
     _("linear, RGB"),
     _("linear, RGB, scene-referred"));
}

int default_group()
{
  return IOP_GROUP_EFFECTS;
}

int flags()
{
  return IOP_FLAGS_INCLUDE_IN_STYLES | IOP_FLAGS_SUPPORTS_BLENDING;
}

dt_iop_colorspace_type_t default_colorspace(dt_iop_module_t *self,
                                            dt_dev_pixelpipe_t *pipe,
                                            dt_dev_pixelpipe_iop_t *piece)
{
  return IOP_CS_RGB;
}

int legacy_params(dt_iop_module_t *self,
                  const void *const old_params,
                  const int old_version,
                  void **new_params,
                  int32_t *new_params_size,
                  int *new_version)
{
  if(old_version == 1)
  {
    typedef struct dt_iop_contrastntexture_params_v1_t
    {
      float gain_local_contrast;
      float detail_level;
      float edge_protection;
      int filter_iterations;
      float noise_bias;
    } dt_iop_contrastntexture_params_v1_t;

    dt_iop_contrastntexture_params_v1_t *o = (dt_iop_contrastntexture_params_v1_t *)old_params;
    dt_iop_contrastntexture_params_t *n = malloc(sizeof(dt_iop_contrastntexture_params_t));
    n->gain_coarse_details = o->gain_local_contrast - 1.0f;
    n->gain_broad_details = o->gain_local_contrast - 1.0f;
    n->gain_medium_details = o->gain_local_contrast - 1.0f;
    n->gain_fine_details = o->gain_local_contrast - 1.0f;
    n->gain_micro_details = o->gain_local_contrast - 1.0f;
    n->detail_level = o->detail_level;
    n->edge_protection = o->edge_protection;
    n->filter_iterations = 1;
    n->noise_bias = o->noise_bias;

    *new_params = n;
    *new_params_size = sizeof(dt_iop_contrastntexture_params_t);
    *new_version = 2;
    return 0;
  }
  return 1;
}

// Compute smoothed luminance mask using edge-aware filters
__DT_CLONE_TARGETS__
static inline void compute_luminance(const float *const restrict in,
                                      float *const restrict luminance,
                                      const dt_iop_roi_t *const roi_in,
                                      const dt_iop_contrastntexture_data_t *const d)
{
  const size_t width = (size_t)roi_in->width;
  const size_t height = (size_t)roi_in->height;
  const size_t npixels = width * height;

  // First compute pixel-wise luminance (no boost) and add noise bias
  luminance_mask(in, luminance, width, height, DT_TONEEQ_NORM_2, 1.0f, 0.0f, 1.0f);
  const float noise_bias = d->noise_bias;

  DT_OMP_FOR()
  for(size_t k = 0; k < npixels; k++)
  {
    luminance[k] += noise_bias;
  }
}

// Compute smoothed luminance mask using edge-aware filters
__DT_CLONE_TARGETS__
static inline void compute_mask(float *const restrict smoothed_luminance,
                                const dt_iop_roi_t *const roi_in,
                                const dt_iop_contrastntexture_data_t *const d,
                                const int level)
{
  const size_t width = (size_t)roi_in->width;
  const size_t height = (size_t)roi_in->height;

  fast_eigf_surface_blur(smoothed_luminance, width, height,
                         d->radius_details[level], d->feathering, d->iterations,
                         DT_GF_BLENDING_LINEAR, 1.0f,
                         0.0f, NORM_MIN, 4.0f);
}

// Extract logarithmic high pass detail in log space (EV):
// How much brighter/darker is this pixel compared to the smooth version
__DT_CLONE_TARGETS__
static inline float extract_details(const float luminance_pixel,
                                   const float luminance_smoothed,
                                   const float noise_bias)
{
  const float log_pixel = log2f(fmaxf(luminance_pixel, NORM_MIN));
  const float log_smoothed = log2f(fmaxf(luminance_smoothed, NORM_MIN));

  const float noise_power = noise_bias * noise_bias;
  const float combined_power = luminance_smoothed * luminance_smoothed;
  const float weiner_gain = fmaxf(combined_power - noise_power, 0.0f) / fmaxf(combined_power, NORM_MIN);
  return weiner_gain * fmaxf(fminf(log_pixel - log_smoothed, 5.0f), -5.0f);
}

// Apply shadow and highlight enhancement
// Different slopes for shadows and highlights, with a smooth transition in the midtones
__DT_CLONE_TARGETS__
static inline float apply_shadows_highlights(const float luminance_lowpass,
                                            const dt_iop_contrastntexture_data_t *const d)
{
  const float slope_shadows = d->slope_shadows;
  const float slope_highlights = d->slope_highlights;
  const float midtones_width = d->midtones_width;
  const float a0 = d->midtones_polynomial[0];
  const float a1 = d->midtones_polynomial[1];
  const float a2 = d->midtones_polynomial[2];
  const float noise_bias = d->noise_bias;
  const float pivot_offset = log2f(noise_bias + 0.1845f);

  float correction_ev = 0.0f;
  // Low pass correction for shadows and highlights
  const float normalized_ev = log2f(fmaxf(luminance_lowpass, NORM_MIN)) - pivot_offset;
  if(normalized_ev <= -midtones_width)
    correction_ev += slope_shadows * normalized_ev;
  else if(normalized_ev >= midtones_width)
    correction_ev += slope_highlights * normalized_ev;
  else
  {
    const float shifted_ev = normalized_ev + midtones_width; // Shift to [0, 2*midtones_width] for polynomial evaluation
    correction_ev += (a0 + a1 * shifted_ev + a2 * shifted_ev * shifted_ev);
  }
  return correction_ev - normalized_ev; // Its only the difference from the identity line that matters for correction
}

/*
 Display the detail mask (difference between pixel and smoothed luminance)
 Output is a grayscale image normalized to [0, 1] where:
 - 0.5 = no local detail (pixel matches neighborhood)
 - < 0.5 = pixel darker than neighborhood
 - > 0.5 = pixel brighter than neighborhood
 */
__DT_CLONE_TARGETS__
static inline void display_local_mask(const float *const restrict luminance_highpass,
                                      const float *const restrict luminance_lowpass,
                                      float *const restrict out,
                                      const dt_iop_roi_t *const roi_in,
                                      const dt_iop_contrastntexture_data_t *const d)
{
  const size_t npixels = (size_t)roi_in->width * roi_in->height;
  const float noise_bias = d->noise_bias;

  DT_OMP_FOR()
  for(size_t k = 0; k < npixels; k++)
  {
    const float local_ev = extract_details(luminance_highpass[k], luminance_lowpass[k], noise_bias);

    // Detail in log space, mapped to [0, 1] for display
    // Detail range roughly [-2, +2] EV mapped to [0, 1]
    const float intensity = local_ev / sqrtf(local_ev * local_ev + 1.0f) * 0.5f + 0.5f; // Smooth mapping to [0, 1]

    // Set all RGB channels to the same intensity (grayscale)
    for_each_channel(c)
    {
      out[4 * k + c] = intensity;
    }
    // Full opacity
    out[4 * k + 3] = 1.0f;
  }
}

void process(dt_iop_module_t *self,
             dt_dev_pixelpipe_iop_t *piece,
             const void *const restrict ivoid,
             void *const restrict ovoid,
             const dt_iop_roi_t *const roi_in,
             const dt_iop_roi_t *const roi_out)
{
  const dt_iop_contrastntexture_data_t *const d = piece->data;
  dt_iop_contrastntexture_gui_data_t *const g = self->gui_data;

  const float *const restrict in = (float *const)ivoid;
  float *const restrict out = (float *const)ovoid;

  const size_t width = roi_in->width;
  const size_t height = roi_in->height;
  const size_t npixels = width * height;

  float *restrict luminance_highpass = dt_alloc_align_float(npixels);
  float *restrict luminance_lowpass = dt_alloc_align_float(npixels);
  float *restrict corrections = dt_alloc_align_float(npixels);

  if(!luminance_lowpass ||
     !luminance_highpass ||
     !corrections)
  {
    dt_control_log(_("contrast and texture failed to allocate memory, check your RAM settings"));
    dt_free_align(luminance_highpass);
    dt_free_align(luminance_lowpass);
    dt_free_align(corrections);
    return;
  }

  // Display output
  bool display_mask = false;
  if(g && g->details_display != DT_LC_MASK_OFF && (piece->pipe->type & DT_DEV_PIXELPIPE_FULL))
  {
    display_mask = true;
    piece->pipe->mask_display = DT_DEV_PIXELPIPE_DISPLAY_PASSTHRU;
  }

  compute_luminance(in, luminance_lowpass, roi_in, d);
  memset(corrections, 0, npixels * sizeof(float));

  for(int level = d->max_level; level >= 0; level--)
  {
    dt_print(DT_DEBUG_PIPE, "Level + %i, Filter radius %i", level, d->radius_details[level]);
    memcpy(luminance_highpass, luminance_lowpass, npixels * sizeof(float));
    compute_mask(luminance_lowpass, roi_in, d, level);
    
    if(display_mask && g->details_display == level)
    {
      // Steal the data to preview as a mask at the correct level and break.
      display_local_mask(luminance_highpass, luminance_lowpass, out, roi_in, d);
      break;
    }
    else
    {
      DT_OMP_FOR()
      for(size_t k = 0; k < npixels; k++)
      {
        // Details as the bandpass difference
        corrections[k] += d->gain_details[level] * extract_details(luminance_highpass[k], luminance_lowpass[k], d->noise_bias);
      }
    }
  }  

  if(!display_mask)
  {
    DT_OMP_FOR()
    for(size_t k = 0; k < npixels; k++)
    {
      // Low pass correction for shadows and highlights
      float lowpass_correction = apply_shadows_highlights(luminance_lowpass[k], d);

      // Apply correction in linear space
      const float multiplier = exp2f(corrections[k] + lowpass_correction);;
      for_each_channel(c)
        out[4 * k + c] = in[4 * k + c] * multiplier;
      out[4 * k + 3] = in[4 * k + 3];
    }
  }

  dt_free_align(luminance_highpass);
  dt_free_align(luminance_lowpass);
  dt_free_align(corrections);
}

void modify_roi_in(dt_iop_module_t *self,
                   dt_dev_pixelpipe_iop_t *piece,
                   const dt_iop_roi_t *roi_out,
                   dt_iop_roi_t *roi_in)
{
  dt_iop_contrastntexture_data_t *const d = piece->data;

  // Get the scaled window radius for the box average
  const float max_size = (float)((piece->iwidth > piece->iheight) ? piece->iwidth : piece->iheight);
  for(int level = 0; level < DT_LC_MASK_LAST; level++)
  {
    const float base_diameter = fminf(d->scale_details[level], 0.5f) * max_size * roi_in->scale;
    const int radius = (int)((base_diameter - 1.0f) / 2.0f);
    d->radius_details[level] = radius;
  }

  // Find the smallest level with non zero radius.
  d->max_level = DT_LC_MASK_LAST - 1;
  for(int level = DT_LC_MASK_LAST - 1; level >= 0; level--)
  {
    dt_print(DT_DEBUG_PIPE, "Max level used %i", d->max_level);
    if(d->radius_details[level] == 0)
    {
      d->max_level = level;
    }
  } 
}

void commit_params(dt_iop_module_t *self,
                   dt_iop_params_t *p1,
                   dt_dev_pixelpipe_t *pipe,
                   dt_dev_pixelpipe_iop_t *piece)
{
  const dt_iop_contrastntexture_params_t *p = (dt_iop_contrastntexture_params_t *)p1;
  dt_iop_contrastntexture_data_t *d = piece->data;

  d->iterations = 1; //p->filter_iterations;
  d->noise_bias = p->noise_bias;

  d->gain_details[0] = p->gain_coarse_details;
  d->gain_details[1] = p->gain_broad_details;
  d->gain_details[2] = p->gain_medium_details;
  d->gain_details[3] = p->gain_fine_details;
  d->gain_details[4] = p->gain_micro_details;
  d->gain_details[5] = p->gain_micro1_details;
  d->gain_details[6] = p->gain_micro2_details;
  d->gain_details[7] = p->gain_micro3_details;
  d->gain_details[8] = p->gain_micro4_details;
  d->gain_details[9] = p->gain_micro5_details;
  d->gain_details[10] = p->gain_micro6_details;
  d->gain_details[11] = p->gain_micro7_details;
  d->gain_details[12] = p->gain_micro8_details;
  d->gain_details[13] = p->gain_micro9_details;
  d->gain_details[14] = p->gain_micro10_details;

  // Log slope of shadows andhighlights, .i.e. the power the modify them with.
  d->slope_shadows = powf(2.0f, -p->gain_shadows);
  d->slope_highlights = powf(2.0f, p->gain_highlights);

  // Quadratic polynomial for the midtones pivot transition.
  // First order smooth with aligned slopes at [0, 2*width]
  d->midtones_width = 0.3f;
  d->midtones_polynomial[0] = -d->slope_shadows * d->midtones_width;
  d->midtones_polynomial[1] = d->slope_shadows;
  d->midtones_polynomial[2] = (d->slope_highlights - d->slope_shadows) / (4.0f * d->midtones_width);

  // UI contrast scale is inverse logarithmic with 0 as 100% of image width.
  // Convert it to a linear scale for processing. Scales are separated by powers of 2 for each step in the UI.
  for(size_t level = 0; level < DT_LC_MASK_LAST; level++)
  {
    d->scale_details[level] = powf(2.0f, -p->detail_level - (float)level);
  }

  // UI feathering is inverted (higher = stricter edge preservation).
  // Adjust the strength based on the number of iterations to maintain a consistent overall effect regardless of iteration count.
  const float default_feathering = 0.2f;  // Base value based on Christian's experiments for a good balance of edge preservation and contrast boost at default settings.
  d->feathering = default_feathering * powf(2.0f, -p->edge_protection) / (d->iterations * d->iterations);
}

static void show_details_callback(GtkWidget *togglebutton, dt_iop_module_t *self)
{
  // early return if blend module is already displaying a mask
  if(self->request_mask_display)
  {
    dt_control_log(_("cannot display masks when the blending mask is displayed"));
    dt_bauhaus_widget_set_quad_active(GTK_WIDGET(togglebutton), FALSE);
    return;
  }

  DT_GUARD_GUI_UPDATE();
  dt_iop_request_focus(self);
  // Activate the module if it wasn't
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->off), TRUE);

  dt_iop_contrastntexture_gui_data_t *g = self->gui_data;
  g->details_display = DT_LC_MASK_OFF;

  const gboolean toggle_is_active = dt_bauhaus_widget_get_quad_active(GTK_WIDGET(togglebutton));
  if(toggle_is_active)
  {
    for(int i = 0; i < DT_LC_MASK_LAST; i++)
    {
      if(togglebutton == g->gain_details[i])
      {
        g->details_display = i;
        break;
      }
    }
  }

  for(int i = 0; i < DT_LC_MASK_LAST; i++)
  {
    dt_bauhaus_widget_set_quad_active(GTK_WIDGET(g->gain_details[i]), g->details_display == i);
  }
  dt_iop_refresh_center(self);
}

void init_pipe(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = calloc(1, sizeof(dt_iop_contrastntexture_data_t));
}

void cleanup_pipe(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  free(piece->data);
  piece->data = NULL;
}

void gui_init(dt_iop_module_t *self)
{
  dt_iop_contrastntexture_gui_data_t *g = IOP_GUI_ALLOC(contrastntexture);
  g->details_display = DT_LC_MASK_OFF;

  // Main container
  self->widget = dt_gui_vbox();

  // Details boost sliders
  char *labels[DT_LC_MASK_LAST] = {_("coarse"), _("broad"), _("medium"), _("fine"), _("micro"), _("micro1"), _("micro2"), _("micro3"), _("micro4"), _("micro5"), _("micro6"), _("micro7"), _("micro8"), _("micro9"), _("micro10")};
  for(int i = 0; i < DT_LC_MASK_LAST; i++)
  {
    char name[256];
    snprintf(name, sizeof(name), "gain_%s_details", labels[i]);
    g->gain_details[i] = dt_bauhaus_slider_from_params(self, name);
    dt_bauhaus_slider_set_soft_range(g->gain_details[i], -1.0, 1.5);
    dt_bauhaus_slider_set_digits(g->gain_details[i], 2);
    dt_bauhaus_slider_set_format(g->gain_details[i], "%");
    dt_bauhaus_slider_set_factor(g->gain_details[i], 100.0);

    gtk_widget_set_tooltip_text(g->gain_details[i],
                              _("amount of contrast enhancement for this detail level"));
    dt_bauhaus_widget_set_quad(g->gain_details[i], self, dtgtk_cairo_paint_showmask, TRUE, show_details_callback,
                             _("preview the details of this level"));
  }

  // Highlights and shadows sliders
  g->gain_highlights = dt_bauhaus_slider_from_params(self, "gain_highlights");
  dt_bauhaus_slider_set_soft_range(g->gain_highlights, -2.0, 2.0);
  gtk_widget_set_tooltip_text(g->gain_highlights,
    _("adjust highlights based on the coarse detail level."));
  g->gain_shadows = dt_bauhaus_slider_from_params(self, "gain_shadows");
  dt_bauhaus_slider_set_soft_range(g->gain_shadows, -2.0, 2.0);
  gtk_widget_set_tooltip_text(g->gain_highlights,
    _("adjust shadows based on the coarse detail level."));

  // Filter settings section
  dt_gui_box_add(self->widget, dt_ui_section_label_new(C_("section", "filter settings")));

  g->detail_level = dt_bauhaus_slider_from_params(self, "detail_level");
  dt_bauhaus_slider_set_soft_range(g->detail_level, 1.0, 10.0);
  gtk_widget_set_tooltip_text(g->detail_level,
     _("base detail level adjusted by the course contrast.\n"
       "higher = more contrast boost in finer details\n"
       "lower = more contrast boost in coarser details"));


  g->edge_protection = dt_bauhaus_slider_from_params(self, "edge_protection");
  dt_bauhaus_slider_set_soft_range(g->edge_protection, -5.0, 5.0);
  dt_bauhaus_slider_set_digits(g->edge_protection, 2);
  dt_bauhaus_slider_set_format(g->edge_protection, "%");
  dt_bauhaus_slider_set_factor(g->edge_protection, 100.0);
  gtk_widget_set_tooltip_text(g->edge_protection, _("adjust the edge sensitivity of the filter\n"
                                                    "higher = more edge preservation\n"
                                                    "lower = smoother transitions, but may lead to halos around edges"));

  // Disable the filter iterations when we test the recursice method                                                  
  // g->filter_iterations = dt_bauhaus_slider_from_params(self, "filter_iterations");
  // dt_bauhaus_slider_set_soft_range(g->filter_iterations, 1, 5);
  // gtk_widget_set_tooltip_text(g->filter_iterations, _("number of passes of the guided filter to apply\n"
  //      "helps diffusing the edges of the filter at the expense of speed"));

  g->noise_bias = dt_bauhaus_slider_from_params(self, "noise_bias");
  dt_bauhaus_slider_set_soft_range(g->noise_bias, 0.0, 0.2);
  dt_bauhaus_slider_set_digits(g->noise_bias, 4);
  dt_bauhaus_slider_set_step(g->noise_bias, 0.0001);
  gtk_widget_set_tooltip_text(g->noise_bias, _("add bias to reduce shadow noise amplification.\n"
                                               "only affects dark parts of the image."));
}

/*
void gui_update(dt_iop_module_t *self)
{
  // Attempt att disabling irrelevant sliders.
  dt_iop_contrastntexture_gui_data_t *g = self->gui_data;
  dt_iop_contrastntexture_params_t *const p = self->params;
  const float iwidth = (float)self->dev->image_storage.width;
  const float iheight = (float)self->dev->image_storage.height;
  const float max_size = fmaxf(iwidth, iheight);

  const float min_scale = (1*2 + 1) / max_size;
  const int highest_used_level = -(int)(log2f(min_scale) + p->detail_level);
  
  //const int highest_used_level = compute_highest_used_detail_level(self, p);

  dt_print(DT_DEBUG_PIPE, "Disabling levels over: %i", highest_used_level);
  for(int i = 0; i < DT_LC_MASK_LAST; i++)
    gtk_widget_set_sensitive(g->gain_details[i], i <= highest_used_level);
}
*/

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
