/*
    This file is part of darktable,
    copyright (c) 2009--2010 johannes hanika.

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
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include <gtk/gtk.h>
#include <inttypes.h>
#ifdef HAVE_GEGL
  #include <gegl.h>
#endif
#include "develop/develop.h"
#include "develop/imageop.h"
#include "control/control.h"
#include "common/image_cache.h"
#include "dtgtk/slider.h"
#include "gui/gtk.h"
#include "gui/draw.h"

#define DCB_DEMOSAIC   6
#define AMAZE_DEMOSAIC 7
#define VCD_DEMOSAIC   8

DT_MODULE(1)

/** core libraw functionality wrapper, for convenience in darkroom mode. */

typedef struct dt_iop_rawimport_params_t
{ // exactly matches dt_image_raw_parameters_t + two floats as in dt_image_t
  float raw_denoise_threshold, raw_auto_bright_threshold;
  unsigned raw_pre_median : 1, raw_wb_cam : 1, raw_greeneq : 1,
           raw_no_auto_bright : 1, raw_demosaic_method : 2,
           raw_med_passes : 4, raw_four_color_rgb : 1,
           raw_highlight : 4,
           fill1 : 9;
  int8_t raw_user_flip;
}
dt_iop_rawimport_params_t;

typedef struct dt_iop_rawimport_gui_data_t
{
  GtkCheckButton *pre_median, *greeneq;
  GtkComboBox *demosaic_method, *highlight;
  // GtkDarktableSlider *denoise_threshold;
  GtkSpinButton *med_passes;
  GtkCheckButton *dcb_enhance;
  GtkWidget *dcb_iterat, *fbdd_noise, *vcd_enhance_lab;
  GtkSpinButton *iterations_dcb;
  GtkSpinButton *noiserd_fbdd;
  GtkCheckButton *vcd_eeci_refine;
  GtkSpinButton *vcd_es_med_passes;
  GtkCheckButton *amaze_ca_correct;
}
dt_iop_rawimport_gui_data_t;

const char *name()
{
  return _("raw import");
}

void process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *i, void *o, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{ // never called, this dummy module is always disabled.
  memcpy(o, i, sizeof(float)*3*roi_out->width*roi_out->height);
}

void commit_params (struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{ // not necessary
}

void init_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = NULL;
  self->commit_params(self, self->default_params, pipe, piece);
}

void cleanup_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
}

static void reimport_button_callback (GtkButton *button, gpointer user_data)
{
  dt_iop_module_t *module = (dt_iop_module_t *)user_data;
  if(module->dt->gui->reset) return;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)module->params;
  pthread_mutex_lock(&module->dev->history_mutex);
  // set image params
  if(p->raw_demosaic_method != 4 && p->raw_demosaic_method != 5) p->raw_four_color_rgb = 0;
  module->dev->image->raw_denoise_threshold      = p->raw_denoise_threshold;
  module->dev->image->raw_auto_bright_threshold  = p->raw_auto_bright_threshold;
  module->dev->image->raw_params.pre_median      = p->raw_pre_median;
  module->dev->image->raw_params.wb_cam          = p->raw_wb_cam;
  module->dev->image->raw_params.no_auto_bright  = p->raw_no_auto_bright;
  module->dev->image->raw_params.demosaic_method = p->raw_demosaic_method;
  module->dev->image->raw_params.med_passes      = p->raw_med_passes;
  module->dev->image->raw_params.four_color_rgb  = p->raw_four_color_rgb;
  module->dev->image->raw_params.greeneq         = p->raw_greeneq;
  module->dev->image->raw_params.highlight       = p->raw_highlight;
  module->dev->image->raw_params.user_flip       = p->raw_user_flip;
  module->dev->image->raw_params.fill0           = p->fill1;
  // also write to db
  dt_image_cache_flush(module->dev->image);
  // force reloading raw image using the params.
  dt_dev_raw_reload(module->dev);
  pthread_mutex_unlock(&module->dev->history_mutex);
  dt_control_gui_queue_draw();
}

static void togglegreeneq_callback (GtkToggleButton *toggle, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  int active = gtk_toggle_button_get_active(toggle);
  p->raw_greeneq = active;
}

static void togglebutton_callback (GtkToggleButton *toggle, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  int active = gtk_toggle_button_get_active(toggle);
  p->raw_pre_median = active;
}

static void demosaic_callback (GtkComboBox *box, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_rawimport_gui_data_t *g = (dt_iop_rawimport_gui_data_t *)self->gui_data;
  if(self->dt->gui->reset) return;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  int active = gtk_combo_box_get_active(box);
  int demosaic = active;
  p->fill1 = (p->fill1&0x1F0) | (active&0xF);
  if(active != 4 && active != 5) p->raw_four_color_rgb = 0;
  if(active == 4)
  {
    active = 0;
    p->raw_four_color_rgb = 1;
  }
  if(active == 5)
  {
    active = 1;
    p->raw_four_color_rgb = 1;
  }
  p->raw_demosaic_method = active;
  gtk_widget_set_visible(GTK_WIDGET(g->dcb_iterat), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->fbdd_noise), FALSE);
  
  gtk_widget_set_visible(GTK_WIDGET(g->dcb_enhance), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->iterations_dcb), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->noiserd_fbdd), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->vcd_eeci_refine), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->vcd_enhance_lab), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->vcd_es_med_passes), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->amaze_ca_correct), FALSE);

  gtk_widget_set_no_show_all(GTK_WIDGET(g->dcb_enhance), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->dcb_iterat), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->iterations_dcb), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->fbdd_noise), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->noiserd_fbdd), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_eeci_refine), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_enhance_lab), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_es_med_passes), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->amaze_ca_correct), TRUE);

  if ( demosaic == DCB_DEMOSAIC ) {
    //g->dcb_enhance = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("dcb enhance")));
    //g->iterations_dcb = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 31, 1)); // not enough room for more then 4
    //g->noiserd_fbdd = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 2, 1));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->dcb_enhance), TRUE);
    gtk_spin_button_set_value(g->iterations_dcb, 0);
    gtk_spin_button_set_value(g->noiserd_fbdd, 0);

    gtk_widget_set_visible(GTK_WIDGET(g->dcb_enhance), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->dcb_iterat), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->iterations_dcb), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->fbdd_noise), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->noiserd_fbdd), TRUE);

    gtk_widget_set_no_show_all(GTK_WIDGET(g->dcb_enhance), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->dcb_iterat), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->iterations_dcb), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->fbdd_noise), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->noiserd_fbdd), FALSE);
  } else if ( demosaic == AMAZE_DEMOSAIC ) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->amaze_ca_correct), FALSE);

    gtk_widget_set_visible(GTK_WIDGET(g->amaze_ca_correct), TRUE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->amaze_ca_correct), FALSE);
  } else if ( demosaic == VCD_DEMOSAIC ) {
    //g->vcd_eeci_refine = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("vcd enhance")));
    //g->vcd_es_med_passes = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 31, 1)); // not enough room for more then 4
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->vcd_eeci_refine), TRUE);
    gtk_spin_button_set_value(g->vcd_es_med_passes, 1);
    gtk_widget_set_visible(GTK_WIDGET(g->vcd_eeci_refine), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->vcd_enhance_lab), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->vcd_es_med_passes), TRUE);

    gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_eeci_refine), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_enhance_lab), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_es_med_passes), FALSE);
  } else {
    p->fill1 = (p->fill1&0x0) | (demosaic&0xF);
  }

}

static void toggledcb_enhance_callback (GtkToggleButton *toggle, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  int active = gtk_toggle_button_get_active(toggle);
  if (active) {
    p->fill1 = p->fill1 | 0x10;
  } else {
    p->fill1 = p->fill1 & 0x1EF;
  }
}

static void iterations_dcb_callback (GtkSpinButton *spin, gpointer user_data)
{
  unsigned short iterations;
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  iterations = gtk_spin_button_get_value(spin);
  if (iterations > 3 ) iterations = 3; // not enough room for parameters
  p->fill1 = (p->fill1&0x19F) | (iterations<<5);
}

static void med_passes_vcd_callback (GtkSpinButton *spin, gpointer user_data)
{
  unsigned short iterations;
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  iterations = gtk_spin_button_get_value(spin);
  if (iterations > 15 ) iterations = 15; // not enough room for parameters
  p->fill1 = (p->fill1&0x1F) | (iterations<<5);
}

static void noiserd_fbdd_callback (GtkSpinButton *spin, gpointer user_data)
{
  unsigned short iterations;
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  iterations = gtk_spin_button_get_value(spin);
  //iterations = iterations<<7;
  p->fill1 = (p->fill1&0x07F) | (iterations<<7);
}

static void median_callback (GtkSpinButton *spin, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  p->raw_med_passes = gtk_spin_button_get_value(spin);
}

#if 0
static void scale_callback (GtkDarktableSlider *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  p->raw_denoise_threshold = dtgtk_slider_get_value(slider);
}
#endif

void gui_update(struct dt_iop_module_t *self)
{
  dt_iop_rawimport_gui_data_t *g = (dt_iop_rawimport_gui_data_t *)self->gui_data;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  // update gui info from params.
  // dtgtk_slider_set_value(g->denoise_threshold, p->raw_denoise_threshold);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->pre_median), p->raw_pre_median);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->greeneq), p->raw_greeneq);
  int dm = p->raw_demosaic_method;
  if ((p->fill1 & 0x0F) != 0 ) {dm =p->fill1 & 0x0F;}
  if(p->raw_four_color_rgb && dm == 0) dm = 4;
  if(p->raw_four_color_rgb && dm == 1) dm = 5;
  gtk_combo_box_set_active(g->demosaic_method, dm);
  gtk_spin_button_set_value(g->med_passes, p->raw_med_passes);
////////////////////////////////
  if(dm != 4 && dm != 5) p->raw_four_color_rgb = 0;
  gtk_widget_set_visible(GTK_WIDGET(g->dcb_iterat), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->fbdd_noise), FALSE);
  
  gtk_widget_set_visible(GTK_WIDGET(g->dcb_enhance), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->iterations_dcb), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->noiserd_fbdd), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->vcd_eeci_refine), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->vcd_enhance_lab), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->vcd_es_med_passes), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(g->amaze_ca_correct), FALSE);

  gtk_widget_set_no_show_all(GTK_WIDGET(g->dcb_enhance), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->dcb_iterat), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->iterations_dcb), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->fbdd_noise), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->noiserd_fbdd), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_eeci_refine), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_enhance_lab), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_es_med_passes), TRUE);
  gtk_widget_set_no_show_all(GTK_WIDGET(g->amaze_ca_correct), TRUE);

  if ( dm == DCB_DEMOSAIC ) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->dcb_enhance), (p->fill1 & 0x010));
    gtk_spin_button_set_value(g->iterations_dcb, (p->fill1 & 0x060)>>5);
    gtk_spin_button_set_value(g->noiserd_fbdd, ((p->fill1 & 0x180)>>7));

    gtk_widget_set_visible(GTK_WIDGET(g->dcb_enhance), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->dcb_iterat), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->iterations_dcb), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->fbdd_noise), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->noiserd_fbdd), TRUE);

    gtk_widget_set_no_show_all(GTK_WIDGET(g->dcb_enhance), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->dcb_iterat), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->iterations_dcb), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->fbdd_noise), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->noiserd_fbdd), FALSE);
  } else if ( dm == AMAZE_DEMOSAIC ) {
    //amaze_ca_correct
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->amaze_ca_correct), (p->fill1 & 0x010));

    gtk_widget_set_visible(GTK_WIDGET(g->amaze_ca_correct), TRUE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->amaze_ca_correct), FALSE);
  } else if ( dm == VCD_DEMOSAIC ) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->vcd_eeci_refine), (p->fill1 & 0x010));
    gtk_spin_button_set_value(g->vcd_es_med_passes, ((p->fill1 & 0x1E0)>>5));

    gtk_widget_set_visible(GTK_WIDGET(g->vcd_eeci_refine), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->vcd_enhance_lab), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(g->vcd_es_med_passes), TRUE);

    gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_eeci_refine), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_enhance_lab), FALSE);
    gtk_widget_set_no_show_all(GTK_WIDGET(g->vcd_es_med_passes), FALSE);
  }
////////////////////////////////
}

static void resetbutton_callback (GtkButton *button, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_rawimport_params_t tmp = (dt_iop_rawimport_params_t)
     {0.0, 0.01, 0, 1, 1, 0, 2, 0, 0, 0, 0, -1};
  memcpy(self->params, &tmp, sizeof(dt_iop_rawimport_params_t));
  dt_iop_rawimport_gui_data_t *g = (dt_iop_rawimport_gui_data_t *)self->gui_data;
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  // dtgtk_slider_set_value(g->denoise_threshold, p->raw_denoise_threshold);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->pre_median), p->raw_pre_median);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->greeneq), p->raw_greeneq);
  int dm = p->raw_demosaic_method;
  if(p->raw_four_color_rgb && dm == 0) dm = 4;
  if(p->raw_four_color_rgb && dm == 1) dm = 5;
  if(dm != 4 && dm != 5) p->raw_four_color_rgb = 0;
  gtk_combo_box_set_active(g->demosaic_method, dm);
  gtk_spin_button_set_value(g->med_passes, p->raw_med_passes);
}

void init(dt_iop_module_t *module)
{
  module->params = malloc(sizeof(dt_iop_rawimport_params_t));
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)module->params;
  module->default_params = malloc(sizeof(dt_iop_rawimport_params_t));
  module->default_enabled = 0;
  module->params_size = sizeof(dt_iop_rawimport_params_t);
  module->gui_data = NULL;
  module->priority = 100;
  module->hide_enable_button = 1;
  p->raw_denoise_threshold     = module->dev->image->raw_denoise_threshold;
  p->raw_auto_bright_threshold = module->dev->image->raw_auto_bright_threshold;
  p->raw_pre_median            = module->dev->image->raw_params.pre_median;
  p->raw_wb_cam                = module->dev->image->raw_params.wb_cam;
  p->raw_no_auto_bright        = module->dev->image->raw_params.no_auto_bright;
  p->raw_demosaic_method       = module->dev->image->raw_params.demosaic_method;
  p->raw_med_passes            = module->dev->image->raw_params.med_passes;
  p->raw_four_color_rgb        = module->dev->image->raw_params.four_color_rgb;
  p->raw_greeneq               = module->dev->image->raw_params.greeneq;
  p->raw_highlight             = module->dev->image->raw_params.highlight;
  p->raw_user_flip             = module->dev->image->raw_params.user_flip;
  p->fill1                     = module->dev->image->raw_params.fill0;
  if(p->raw_demosaic_method != 4 && p->raw_demosaic_method != 5 && p->raw_demosaic_method != 0 && p->raw_demosaic_method != 1) p->raw_four_color_rgb = 0;
  memcpy(module->default_params, p, sizeof(dt_iop_rawimport_params_t));
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL;
  free(module->params);
  module->params = NULL;
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_rawimport_gui_data_t));
  dt_iop_rawimport_gui_data_t *g = (dt_iop_rawimport_gui_data_t *)self->gui_data;

  self->widget = gtk_table_new(8, 10, FALSE);

  GtkWidget *label;
  label = gtk_label_new(_("warning: these are cryptic low level settings!\nyou probably don't have to change them."));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(self->widget), label, 0, 7, 0, 1, GTK_EXPAND|GTK_FILL, 0, 0, 0);

#if 0
  label = gtk_label_new(_("denoise"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start(GTK_BOX(vbox1), label, TRUE, TRUE, 0);
  g->denoise_threshold = DTGTK_SLIDER(dtgtk_slider_new_with_range(DARKTABLE_SLIDER_BAR,0.0, 1000.0, 1.0,0,1));
  gtk_box_pack_start(GTK_BOX(vbox2), GTK_WIDGET(g->denoise_threshold), TRUE, TRUE, 0);
#endif

  label = gtk_label_new(_("median passes"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  g->med_passes = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 31, 1));
  gtk_object_set(GTK_OBJECT(g->med_passes), "tooltip-text", _("number of 3x3 median filter passes\non R-G and B-G after demosaicing"), NULL);
  gtk_table_attach(GTK_TABLE(self->widget), label, 0, 3, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->med_passes), 4, 7, 1, 2, GTK_EXPAND|GTK_FILL, 0, 0, 0);

  label = gtk_label_new(_("demosaic method"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  g->demosaic_method = GTK_COMBO_BOX(gtk_combo_box_new_text());
  gtk_combo_box_append_text(g->demosaic_method, C_("demosaic", "linear"));
  gtk_combo_box_append_text(g->demosaic_method, _("VNG"));
  gtk_combo_box_append_text(g->demosaic_method, _("PPG"));
  gtk_combo_box_append_text(g->demosaic_method, _("AHD"));
  gtk_combo_box_append_text(g->demosaic_method, _("linear4"));
  gtk_combo_box_append_text(g->demosaic_method, _("VNG4"));
  gtk_combo_box_append_text(g->demosaic_method, _("DCB"));
  gtk_combo_box_append_text(g->demosaic_method, _("AMaZE"));
  gtk_combo_box_append_text(g->demosaic_method, _("VCD"));
  gtk_table_attach(GTK_TABLE(self->widget), label, 0, 3, 2, 3, GTK_EXPAND|GTK_FILL, 0, 0, 0);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->demosaic_method), 4, 7, 2, 3, GTK_EXPAND|GTK_FILL, 0, 0, 0);

  g->pre_median = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("hot pixels")));
  gtk_object_set(GTK_OBJECT(g->pre_median), "tooltip-text", _("median filter before demosaicing"), NULL);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->pre_median), 4, 7, 3, 4, GTK_EXPAND|GTK_FILL, 0, 0, 0);

  g->greeneq = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("green equilibration")));
  gtk_object_set(GTK_OBJECT(g->greeneq), "tooltip-text", _("work around unequal green channels in some mid-range\ncameras, e.g. canon eos 400d"), NULL);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->greeneq), 4, 7, 4, 5, GTK_EXPAND|GTK_FILL, 0, 0, 0);

  // GtkCheckButton *dcb_enhance;
  g->dcb_enhance = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("dcb enhance")));
  gtk_object_set(GTK_OBJECT(g->dcb_enhance), "tooltip-text", _("turns off the default image refinement if edges are jagged or you prefer softer demosaicing.\nworks only with DCB."), NULL);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->dcb_enhance), 4, 7, 6, 7, GTK_EXPAND|GTK_FILL, 0, 0, 0);

  // GtkSpinButton *iterations_dcb;
  g->dcb_iterat = gtk_label_new(_("dcb iterations"));
  gtk_misc_set_alignment(GTK_MISC(g->dcb_iterat), 0.0, 0.5);
  g->iterations_dcb = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 31, 1)); // not enough room for number more then 4
  gtk_object_set(GTK_OBJECT(g->iterations_dcb), "tooltip-text", _("number of DCB correction routines passes(the default is zero)\nthis helps if you see lots of wrong interpolation directions, just remember that it may produce new artifacts\nusually 0 or 1 should work fine, in some situations even 10 is not enough. works only with DCB."), NULL);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->dcb_iterat), 0, 3, 7, 8, GTK_EXPAND|GTK_FILL, 0, 0, 0);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->iterations_dcb), 4, 7, 7, 8, GTK_EXPAND|GTK_FILL, 0, 0, 0);

  //GtkSpinButton *noiserd_fbdd;
  g->fbdd_noise = gtk_label_new(_("fbdd denoising"));
  gtk_misc_set_alignment(GTK_MISC(g->fbdd_noise), 0.0, 0.5);
  g->noiserd_fbdd = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 2, 1));
  gtk_object_set(GTK_OBJECT(g->noiserd_fbdd), "tooltip-text", _("1-turns on pre demosaicing FBDD image denoising\n2-with additional chroma noise reduction.\nworks with all demosaicing methods"), NULL);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->fbdd_noise), 0, 3, 8, 9, GTK_EXPAND|GTK_FILL, 0, 0, 0);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->noiserd_fbdd), 4, 7, 8, 9, GTK_EXPAND|GTK_FILL, 0, 0, 0);

  // GtkCheckButton *vcd_eeci_refine;
  g->vcd_eeci_refine = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("vcd enhance")));
  gtk_object_set(GTK_OBJECT(g->vcd_eeci_refine), "tooltip-text", _("refine interpolated pixels"), NULL);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->vcd_eeci_refine), 4, 7, 6, 7, GTK_EXPAND|GTK_FILL, 0, 0, 0);

  // GtkSpinButton *vcd_es_med_passes;
  g->vcd_enhance_lab = gtk_label_new(_("edge median passes"));
  gtk_misc_set_alignment(GTK_MISC(g->vcd_enhance_lab), 0.0, 0.5);
  g->vcd_es_med_passes = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 31, 1)); // not enough room for more then 4
  gtk_object_set(GTK_OBJECT(g->vcd_es_med_passes), "tooltip-text", _("number of passes an edge-sensitive median filter to differential color planes"), NULL);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->vcd_enhance_lab), 0, 3, 7, 8, GTK_EXPAND|GTK_FILL, 0, 0, 0);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->vcd_es_med_passes), 4, 7, 7, 8, GTK_EXPAND|GTK_FILL, 0, 0, 0);

  //GtkCheckButton *amaze_ca_correct;
  g->amaze_ca_correct = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(_("ca autocorrect")));
  gtk_object_set(GTK_OBJECT(g->amaze_ca_correct), "tooltip-text", _("artificial ca autocorrection from AMaZE"), NULL);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(g->amaze_ca_correct), 4, 7, 6, 7, GTK_EXPAND|GTK_FILL, 0, 0, 0);

  GtkWidget *reset = gtk_button_new_with_label(_("reset"));
  gtk_object_set(GTK_OBJECT(reset), "tooltip-text", _("reset raw loading parameters\nto darktable's defaults"), NULL);
  GtkWidget *reload = gtk_button_new_with_label(_("re-import"));
  gtk_object_set(GTK_OBJECT(reload), "tooltip-text", _("trigger re-import of the raw image"), NULL);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(reset), 0, 3, 10, 11, GTK_EXPAND|GTK_FILL, 0, 0, 0);
  gtk_table_attach(GTK_TABLE(self->widget), GTK_WIDGET(reload), 4, 7, 10, 11, GTK_EXPAND|GTK_FILL, 0, 0, 0);
  
  // self->gui_update(self);
  dt_iop_rawimport_params_t *p = (dt_iop_rawimport_params_t *)self->params;
  // dtgtk_slider_set_value(g->denoise_threshold, p->raw_denoise_threshold);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->pre_median), p->raw_pre_median);
  gtk_combo_box_set_active(g->demosaic_method, p->raw_demosaic_method);
  gtk_spin_button_set_value(g->med_passes, p->raw_med_passes);

  g_signal_connect (G_OBJECT (g->pre_median), "toggled", G_CALLBACK (togglebutton_callback), self);
  g_signal_connect (G_OBJECT (g->greeneq), "toggled", G_CALLBACK (togglegreeneq_callback), self);
  g_signal_connect (G_OBJECT (g->demosaic_method), "changed", G_CALLBACK (demosaic_callback), self);
  g_signal_connect (G_OBJECT (g->med_passes), "value-changed", G_CALLBACK (median_callback), self);
  // g_signal_connect (G_OBJECT (g->denoise_threshold),     "value-changed", G_CALLBACK (scale_callback), self);
  g_signal_connect (G_OBJECT (reload),     "clicked", G_CALLBACK (reimport_button_callback), self);
  g_signal_connect (G_OBJECT (reset),      "clicked", G_CALLBACK (resetbutton_callback), self);

  g_signal_connect (G_OBJECT (g->dcb_enhance), "toggled", G_CALLBACK (toggledcb_enhance_callback), self);
  g_signal_connect (G_OBJECT (g->iterations_dcb), "value-changed", G_CALLBACK (iterations_dcb_callback), self);
  g_signal_connect (G_OBJECT (g->noiserd_fbdd), "value-changed", G_CALLBACK (noiserd_fbdd_callback), self);

  g_signal_connect (G_OBJECT (g->vcd_eeci_refine), "toggled", G_CALLBACK (toggledcb_enhance_callback), self);
  g_signal_connect (G_OBJECT (g->vcd_es_med_passes), "value-changed", G_CALLBACK (med_passes_vcd_callback), self);

  g_signal_connect (G_OBJECT (g->amaze_ca_correct), "toggled", G_CALLBACK (toggledcb_enhance_callback), self);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  free(self->gui_data);
  self->gui_data = NULL;
}

