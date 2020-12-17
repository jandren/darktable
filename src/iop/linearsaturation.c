/*
    This file is part of darktable,
    Copyright (C) 2010-2020 darktable developers.

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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
// our includes go first:
#include "bauhaus/bauhaus.h"
#include "common/rgb_norms.h"
#include "develop/imageop.h"
#include "develop/imageop_gui.h"
#include "gui/color_picker_proxy.h"
#include "gui/gtk.h"
#include "iop/iop_api.h"

#include <gtk/gtk.h>
#include <stdlib.h>

// This is an example implementation of an image operation module that does nothing useful.
// It demonstrates how the different functions work together. To build your own module,
// take all of the functions that are mandatory, stripping them of comments.
// Then add only the optional functions that are required to implement the functionality
// you need. Don't copy default implementations (hint: if you don't need to change or add
// anything, you probably don't need the copy). Make sure you choose descriptive names
// for your fields and variables. The ones given here are just examples; rename them.
//
// To have your module compile and appear in darkroom, add it to CMakeLists.txt, with
//  add_iop(linearsaturation "linearsaturation.c")
// and to iop_order.c, in the initialisation of legacy_order & v30_order with:
//  { {XX.0f }, "linearsaturation", 0},

// This is the version of the module's parameters,
// and includes version information about compile-time dt.
// The first released version should be 1.
DT_MODULE_INTROSPECTION(1, dt_iop_linearsaturation_params_t)

// TODO: some build system to support dt-less compilation and translation!

// Enums used in params_t can have $DESCRIPTIONs that will be used to
// automatically populate a combobox with dt_bauhaus_combobox_from_params.
// They are also used in the history changes tooltip.
// Combobox options will be presented in the same order as defined here.
// These numbers must not be changed when a new version is introduced.
typedef enum dt_iop_linearsaturation_type_t
{
  DT_LINEARSATURATION_Y = 0,           // $DESCRIPTION: "Luminance Y"
  DT_LINEARSATURATION_AVERAGE = 1,     // $DESCRIPTION: "Average"
  DT_LINEARSATURATION_NORM = 2,        // $DESCRIPTION: "Vector Norm"
  DT_LINEARSATURATION_POWER_NORM = 3,  // $DESCRIPTION: "Power Norm"
  DT_LINEARSATURATION_ACES = 4,        // $DESCRIPTION: "ACES Luminance"
} dt_iop_linearsaturation_type_t;

typedef struct dt_iop_linearsaturation_params_t
{
  // The parameters defined here fully record the state of the module and are stored
  // (as a serialized binary blob) into the db.
  // Make sure everything is in here does not depend on temporary memory (pointers etc).
  // This struct defines the layout of self->params and self->default_params.
  // You should keep changes to this struct to a minimum.
  // If you have to change this struct, it will break
  // user data bases, and you have to increment the version
  // of DT_MODULE_INTROSPECTION(VERSION) above and provide a legacy_params upgrade path!
  //
  // Tags in the comments get picked up by the introspection framework and are
  // used in gui_init to set range and labels (for widgets and history)
  // and value checks before commit_params.
  // If no explicit init() is specified, the default implementation uses $DEFAULT tags
  // to initialise self->default_params, which is then used in gui_init to set widget defaults.

  float saturation_factor;   // $MIN: 0.0 $MAX: 2.0 $DEFAULT: 0.96
  dt_iop_rgb_norms_t luma_method; // $DEFAULT: DT_RGB_NORM_LUMINANCE $DESCRIPTION: "Grey"
} dt_iop_linearsaturation_params_t;

typedef struct dt_iop_linearsaturation_gui_data_t
{
  // Whatever you need to make your gui happy and provide access to widgets between gui_init, gui_update etc.
  // Stored in self->gui_data while in darkroom.
  // To permanently store per-user gui configuration settings, you could use dt_conf_set/_get.
  GtkWidget *saturation_slider, *luminance_method;
} dt_iop_linearsaturation_gui_data_t;

typedef struct dt_iop_linearsaturation_global_data_t
{
  // This is optionally stored in self->global_data
  // and can be used to alloc globally needed stuff
  // which is needed in gui mode and during processing.

  // We don't need it for this example (as for most dt plugins).
} dt_iop_linearsaturation_global_data_t;

// this returns a translatable name
const char *name()
{
  // make sure you put all your translatable strings into _() !
  return _("linear saturation");
}

// some additional flags (self explanatory i think):
int flags()
{
  return IOP_FLAGS_INCLUDE_IN_STYLES | IOP_FLAGS_SUPPORTS_BLENDING;
}

// where does it appear in the gui?
int default_group()
{
  return IOP_GROUP_BASIC | IOP_GROUP_TECHNICAL;
}

int default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  return iop_cs_rgb;
}

// Whenever new fields are added to (or removed from) dt_iop_..._params_t or when their meaning
// changes, a translation from the old to the new version must be added here.
// A verbatim copy of the old struct definition should be copied into the routine with a _v?_t ending.
// Since this will get very little future testing (because few developers still have very
// old versions lying around) existing code should be changed as little as possible, if at all.
//
// Upgrading from an older version than the previous one should always go through all in between versions
// (unless there was a bug) so that the end result will always be the same.
//
// Be careful with changes to structs that are included in _params_t
//
// Individually copy each existing field that is still in the new version. This is robust even if reordered.
// If only new fields were added at the end, one call can be used:
//   memcpy(n, o, sizeof *o);
//
// Hardcode the default values for new fields that were added, rather than referring to default_params;
// in future, the field may not exist anymore or the default may change. The best default for a new version
// to replicate a previous version might not be the optimal default for a fresh image.
//
// FIXME: the calling logic needs to be improved to call upgrades from consecutive version in sequence.
int legacy_params(dt_iop_module_t *self, const void *const old_params, const int old_version,
                  void *new_params, const int new_version)
{
  // typedef dt_iop_linearsaturation_params_t dt_iop_linearsaturation_params_v3_t; // always create copy of current so code below doesn't need to be touched

  return 1;
}

static const int mask_id = 1; // key "0" is reserved for the pipe
static const char *mask_name = "linearsaturation checkerboard";

void commit_params(dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  memcpy(piece->data, p1, self->params_size);

  // there is no real need for this, but if the number of masks can be changed by the user this is the way to go.
  // otherwise we can have old stale masks floating around
  g_hash_table_remove_all(self->raster_mask.source.masks);
  g_hash_table_insert(self->raster_mask.source.masks, GINT_TO_POINTER(mask_id), g_strdup(mask_name));
}

/** modify regions of interest (optional, per pixel ops don't need this) */
// void modify_roi_out(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, dt_iop_roi_t
// *roi_out, const dt_iop_roi_t *roi_in);
// void modify_roi_in(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, const dt_iop_roi_t
// *roi_out, dt_iop_roi_t *roi_in);
// void distort_mask(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, const float *const in,
// float *const out, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out);
/*
static float luma_y(const float *const rgb_in)
{
  float pixel_xyz[3];
  dt_prophotorgb_to_XYZ(rgb_in, pixel_xyz);
  return pixel_xyz[1]; // the Y channel is the relative luminance
}

static float luma_average(const float *const rgb_in)
{
  return (rgb_in[0] + rgb_in[1] + rgb_in[2]) / 3.0f;
}

static float luma_grey(const float *const rgb_in)
{
  return 0.18;
}

static float luma_max(const float *const rgb_in)
{
  return fmaxf(rgb_in[0], fmaxf(rgb_in[1], rgb_in[2]));
}

static float luma_vector_norm(const float *const rgb_in)
{
  return powf(rgb_in[0] * rgb_in[0] + rgb_in[1] * rgb_in[1] + rgb_in[2] * rgb_in[2], 0.5f);
}

static float luma_power_norm(const float *const rgb_in)
{
  float R, G, B;
  R = rgb_in[0] * rgb_in[0];
  G = rgb_in[1] * rgb_in[1];
  B = rgb_in[2] * rgb_in[2];
  return (rgb_in[0] * R + rgb_in[1] * G + rgb_in[2] * B) / (R + G + B);
}

static float luma_aces(const float *const rgb_in)
{
  // Converts RGB to a luminance proxy, here called YC
  // YC is ~ Y + K * Chroma
  // Constant YC is a cone-shaped surface in RGB space, with the tip on the 
  // neutral axis, towards white.
  // YC is normalized: RGB 1 1 1 maps to YC = 1
  //
  // ycRadiusWeight defaults to 1.75, although can be overridden in function 
  // call to rgb_2_yc
  // ycRadiusWeight = 1 -> YC for pure cyan, magenta, yellow == YC for neutral 
  // of same value
  // ycRadiusWeight = 2 -> YC for pure red, green, blue  == YC for  neutral of 
  // same value.
  const float ycRadiusWeight = 1.75;
  const float r = rgb_in[0]; 
  const float g = rgb_in[1]; 
  const float b = rgb_in[2];
  
  const float chroma = sqrt(b*(b-g)+g*(g-r)+r*(r-b));

  return ( b + g + r + ycRadiusWeight * chroma) / 3.;
}*/

/** process, all real work is done here. */
void process(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const ivoid, void *const ovoid,
             const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  // this is called for preview and full pipe separately, each with its own pixelpipe piece.
  // get our data struct:
  dt_iop_linearsaturation_params_t *d = (dt_iop_linearsaturation_params_t *)piece->data;
  const dt_iop_order_iccprofile_info_t *const work_profile = dt_ioppr_get_pipe_work_profile_info(piece->pipe);

  const float *const in = (const float *)ivoid;
  float *const out = (float *)ovoid;

  const int width = roi_in->width, height = roi_in->height;
  for(size_t k = 0; k < (size_t)width * height; k++)
  {
    const float *pixel_in_rgba = in + 4 * k;
    float *pixel_out_rgba = out + 4 * k;
    const float luma = dt_rgb_norm(pixel_in_rgba, d->luma_method, work_profile);
    for(int c = 0; c < 3; c++)
    {
      pixel_out_rgba[c] = luma + d->saturation_factor * (pixel_in_rgba[c] - luma);
    }
    // Multiple the saturation, x and z channels
    // pixel_xyz[0] *= d->saturation_factor;
    // pixel_xyz[2] *= d->saturation_factor;
    // dt_XYZ_to_prophotorgb(pixel_xyz, pixel_out_rgb);

    // Pass through of the alpha channel
    pixel_out_rgba[3] = pixel_in_rgba[3];
  }
}

/** Optional init and cleanup */
void init(dt_iop_module_t *module)
{
  // Allocates memory for a module instance and fills default_params.
  // If this callback is not provided, the standard implementation in
  // dt_iop_default_init is used, which looks at the $DEFAULT introspection tags
  // in the comments of the params_t struct definition.
  // An explicit implementation of init is only required if not all fields are
  // fully supported by dt_iop_default_init, for example arrays with non-identical values.
  // In that case, dt_iop_default_init can be called first followed by additional initialisation.
  // The values in params will not be used and default_params can be overwritten by
  // reload_params on a per-image basis.
  dt_iop_default_init(module);

  // Any non-default settings; for example disabling the on/off switch:
  // To make this work correctly, you also need to hide the widgets, otherwise moving one
  // would enable the module anyway. The standard way is to set up a gtk_stack and show
  // the page that only has a label with an explanatory text when the module can't be used.
}

void init_global(dt_iop_module_so_t *module)
{
  module->data = malloc(sizeof(dt_iop_linearsaturation_global_data_t));
}

void cleanup(dt_iop_module_t *module)
{
  // Releases any memory allocated in init(module)
  // Implement this function explicitly if the module allocates additional memory besides (default_)params.
  // this is rare.
  free(module->params);
  module->params = NULL;
  free(module->default_params);
  module->default_params = NULL;
}

void cleanup_global(dt_iop_module_so_t *module)
{
  free(module->data);
  module->data = NULL;
}

/** optional gui callbacks. */
void gui_changed(dt_iop_module_t *self, GtkWidget *w, void *previous)
{
  // If defined, this gets called when any of the introspection based widgets
  // (created with dt_bauhaus_..._from_params) are changed.
  // The updated value from the widget is already set in params.
  // any additional side-effects can be achieved here.
  //dt_iop_linearsaturation_params_t *p = (dt_iop_linearsaturation_params_t *)self->params;
  //dt_iop_linearsaturation_gui_data_t *g = (dt_iop_linearsaturation_gui_data_t *)self->gui_data;
}

/** gui setup and update, these are needed. */
void gui_update(dt_iop_module_t *self)
{
  // This gets called when switching to darkroom, with each image change or when
  // a different history item is selected.
  // Here, all the widgets need to be set to the current values in param.
  //
  // Note, this moves data from params -> gui. All fields at same time.
  // The opposite direction, gui -> params happens one field at a time, for example
  // when the user manipulates a slider. It is handled by gui_changed (and the
  // automatic callback) for introspection based widgets or by the explicit callback
  // set up manually (see example of extra_callback above).
  dt_iop_linearsaturation_gui_data_t *g = (dt_iop_linearsaturation_gui_data_t *)self->gui_data;
  dt_iop_linearsaturation_params_t *p = (dt_iop_linearsaturation_params_t *)self->params;

  dt_bauhaus_slider_set(g->saturation_slider, p->saturation_factor);

  // Use set_from_value to correctly handle out of order values.
  dt_bauhaus_combobox_set_from_value(g->luminance_method, p->luma_method);

  // Any configuration changes to the gui that depend on field values should be done here,
  // or can be done in gui_changed which can then be called from here with widget == NULL.
  gui_changed(self, NULL, NULL);
}

/** optional: if this exists, it will be called to init new defaults if a new image is
 * loaded from film strip mode. */
void reload_defaults(dt_iop_module_t *module)
{
  // This only has to be provided if module settings or default_params need to depend on
  // image type (raw?) or exif data.
  // Make sure to always reset to the default for non-special cases, otherwise the override
  // will stick when switching to another image.
  dt_iop_linearsaturation_params_t *d = (dt_iop_linearsaturation_params_t *)module->default_params;

  // As an example, switch off for non-raw images. The enable button was already hidden in init().
  if(!dt_image_is_raw(&module->dev->image_storage))
  {
    module->default_enabled = 0;
  }
  else
  {
    module->default_enabled = 1;
  }

  // If we are in darkroom, gui_init will already have been called and has initialised
  // module->gui_data and widgets.
  // So if deafault values have been changed, it may then be necessary to also change the
  // default values in widgets. Resetting the individual widgets will then have the same
  // effect as resetting the whole module at once.
  dt_iop_linearsaturation_gui_data_t *g = (dt_iop_linearsaturation_gui_data_t *)module->gui_data;
  if(g)
  {
    dt_bauhaus_slider_set_default(g->saturation_slider, d->saturation_factor);
  }
}

void gui_init(dt_iop_module_t *self)
{
  // Allocates memory for the module's user interface in the darkroom and
  // sets up the widgets in it.
  //
  // self->widget needs to be set to the top level widget.
  // This can be a (vertical) box, a grid or even a notebook. Modules that are
  // disabled for certain types of images (for example non-raw) may use a stack
  // where one of the pages contains just a label explaining why it is disabled.
  //
  // Widgets that are directly linked to a field in params_t may be set up using the
  // dt_bauhaus_..._from_params family. They take a string with the field
  // name in the params_t struct definition. The $MIN, $MAX and $DEFAULT tags will be
  // used to set up the widget (slider) ranges and default values and the $DESCRIPTION
  // is used as the widget label.
  //
  // The _from_params calls also set up an automatic callback that updates the field in params
  // whenever the widget is changed. In addition, gui_changed is called, if it exists,
  // so that any other required changes, to dependent fields or to gui widgets, can be made.
  //
  // Whenever self->params changes (switching images or history) the widget values have to
  // be updated in gui_update.
  //
  // Do not set the value of widgets or configure them depending on field values here;
  // this should be done in gui_update (or gui_changed or individual widget callbacks)
  //
  // If any default values for (slider) widgets or options (in comboboxes) depend on the
  // type of image, then the widgets have to be updated in reload_params.
  dt_iop_linearsaturation_gui_data_t *g = IOP_GUI_ALLOC(linearsaturation);

  // If the first widget is created useing a _from_params call, self->widget does not have to
  // be explicitly initialised, as a new virtical box will be created automatically.
  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, DT_BAUHAUS_SPACE);

  // Linking a slider to an integer will make it take only whole numbers (step=1).
  // The new slider is added to self->widget
  g->saturation_slider = dt_bauhaus_slider_from_params(self, "saturation_factor");

  // A combobox linked to struct field will be filled with the values and $DESCRIPTIONs
  // in the struct definition, in the same order. The automatic callback will put the
  // enum value, not the position within the combobox list, in the field.
  g->luminance_method = dt_bauhaus_combobox_from_params(self, "luma_method");
}

void gui_cleanup(dt_iop_module_t *self)
{
  // This only needs to be provided if gui_init allocates any memory or resources besides
  // self->widget and gui_data_t. The default function (if an explicit one isn't provided here)
  // takes care of gui_data_t (and gtk destroys the widget anyway). If you override the default,
  // you have to do whatever you have to do, and also call IOP_GUI_FREE to clean up gui_data_t.

  IOP_GUI_FREE;
}

/** additional, optional callbacks to capture darkroom center events. */
// void gui_post_expose(dt_iop_module_t *self, cairo_t *cr, int32_t width, int32_t height, int32_t pointerx,
// int32_t pointery);
// int mouse_moved(dt_iop_module_t *self, double x, double y, double pressure, int which);
// int button_pressed(dt_iop_module_t *self, double x, double y, double pressure, int which, int type,
// uint32_t state);
// int button_released(struct dt_iop_module_t *self, double x, double y, int which, uint32_t state);
// int scrolled(dt_iop_module_t *self, double x, double y, int up, uint32_t state);

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
