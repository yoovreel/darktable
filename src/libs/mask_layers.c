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

// masks: every masked adjustment of this photo in one list, like layers.
// A row is a module instance with a drawn, parametric or raster mask; click
// it to go to it with its mask panel open, use its switch to turn it on or
// off. "new mask" starts one more (a masked exposure instance).

#include "common/darktable.h"
#include "control/conf.h"
#include "control/control.h"
#include "control/signal.h"
#include "develop/blend.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "dtgtk/button.h"
#include "dtgtk/paint.h"
#include "dtgtk/togglebutton.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"

DT_MODULE(1)

typedef struct dt_lib_mask_layers_t
{
  GtkWidget *list;
  GtkWidget *empty;
  guint refresh;
  guint poll;
  gchar *signature;
} dt_lib_mask_layers_t;

const char *name(dt_lib_module_t *self)
{
  return _("masks");
}

const char *description(dt_lib_module_t *self)
{
  return _("the masked adjustments of this photo, like layers:\n"
           "click one to edit it, or start a new mask");
}

dt_view_type_flags_t views(dt_lib_module_t *self)
{
  return DT_VIEW_DARKROOM;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}

int position(const dt_lib_module_t *self)
{
  // under the presets (1100)
  return 1050;
}

static gboolean _has_mask(const dt_iop_module_t *m)
{
  return m->blend_params
         && (m->blend_params->mask_mode & (DEVELOP_MASK_MASK | DEVELOP_MASK_FLEXI
                                           | DEVELOP_MASK_CONDITIONAL | DEVELOP_MASK_RASTER));
}

static void _row_clicked(GtkButton *button, dt_lib_module_t *self)
{
  dt_iop_module_t *m = g_object_get_data(G_OBJECT(button), "module");
  dt_develop_t *dev = darktable.develop;
  if(!m || !dev || !g_list_find(dev->iop, m)) return;
  dt_dev_modulegroups_switch(dev, m);
  dt_iop_request_focus(m);
  dt_iop_gui_set_expanded(m, TRUE, TRUE);
  dt_iop_gui_blend_masks_panel_open();
}

static void _switch_toggled(GtkToggleButton *button, dt_lib_module_t *self)
{
  dt_iop_module_t *m = g_object_get_data(G_OBJECT(button), "module");
  dt_develop_t *dev = darktable.develop;
  if(!m || !dev || !g_list_find(dev->iop, m) || !m->off) return;
  const gboolean on = gtk_toggle_button_get_active(button);
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->off)) != on)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->off), on);
}

static void _new_clicked(GtkButton *button, dt_lib_module_t *self)
{
  if(dt_iop_gui_blend_new_mask_adjustment())
    dt_iop_gui_blend_masks_panel_open();
  else
    dt_control_log(_("could not start a new mask"));
}

static void _fill(dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = self->data;
  dt_gui_container_destroy_children(GTK_CONTAINER(d->list));
  dt_develop_t *dev = darktable.develop;
  int n = 0;

  // top of the pipe first, as layers are listed
  for(const GList *l = dev ? g_list_last(dev->iop) : NULL; l; l = g_list_previous(l))
  {
    dt_iop_module_t *m = l->data;
    if(!m->expander || !_has_mask(m) || dt_iop_is_hidden(m)) continue;

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    dt_gui_add_class(row, "dt_mask_layer");

    GtkWidget *sw = dtgtk_togglebutton_new(dtgtk_cairo_paint_switch, 0, NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sw), m->enabled);
    gtk_widget_set_tooltip_text(sw, _("turn this adjustment on or off"));
    g_object_set_data(G_OBJECT(sw), "module", m);
    g_signal_connect(sw, "toggled", G_CALLBACK(_switch_toggled), self);
    dt_gui_add_class(sw, "dt_transparent_background");

    const gboolean named = m->multi_name[0] && strcmp(m->multi_name, "0");
    gchar *text = named
      ? g_markup_printf_escaped("%s  <span alpha=\"60%%\">%s</span>", m->multi_name, m->name())
      : g_markup_printf_escaped("%s", m->name());
    GtkWidget *btn = gtk_button_new();
    GtkWidget *lb = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lb), text);
    g_free(text);
    gtk_label_set_xalign(GTK_LABEL(lb), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(lb), PANGO_ELLIPSIZE_END);
    gtk_container_add(GTK_CONTAINER(btn), lb);
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(btn, _("edit this adjustment and its mask"));
    g_object_set_data(G_OBJECT(btn), "module", m);
    g_signal_connect(btn, "clicked", G_CALLBACK(_row_clicked), self);
    if(m == dt_dev_gui_module()) dt_gui_add_class(row, "dt_mask_layer_active");

    gtk_box_pack_start(GTK_BOX(row), sw, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(d->list), row, FALSE, FALSE, 0);
    n++;
  }

  gtk_widget_set_visible(d->empty, n == 0);
  gtk_widget_show_all(d->list);
}

// what the list shows, to rebuild it only when that changes. Mask edits do
// not all raise a signal, so this is also checked on a timer
static gchar *_signature(void)
{
  GString *sig = g_string_new(NULL);
  dt_develop_t *dev = darktable.develop;
  for(const GList *l = dev ? dev->iop : NULL; l; l = g_list_next(l))
  {
    const dt_iop_module_t *m = l->data;
    if(!m->expander || !_has_mask(m)) continue;
    g_string_append_printf(sig, "%p:%d:%s:%d|", (void *)m, m->enabled, m->multi_name,
                           m == dt_dev_gui_module());
  }
  return g_string_free(sig, FALSE);
}

static gboolean _poll(gpointer user_data)
{
  dt_lib_module_t *self = user_data;
  dt_lib_mask_layers_t *d = self->data;
  if(!self->widget || !gtk_widget_get_mapped(self->widget)) return G_SOURCE_CONTINUE;
  gchar *sig = _signature();
  if(g_strcmp0(sig, d->signature))
  {
    g_free(d->signature);
    d->signature = sig;
    _fill(self);
  }
  else
    g_free(sig);
  return G_SOURCE_CONTINUE;
}

static gboolean _refresh_idle(gpointer user_data)
{
  dt_lib_module_t *self = user_data;
  dt_lib_mask_layers_t *d = self->data;
  d->refresh = 0;
  _fill(self);
  return G_SOURCE_REMOVE;
}

static void _queue_refresh(gpointer instance, dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = self->data;
  if(!d->refresh) d->refresh = g_timeout_add(150, _refresh_idle, self);
}

void gui_init(dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = g_malloc0(sizeof(dt_lib_mask_layers_t));
  self->data = d;

  d->list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_name(d->list, "mask-layers");

  d->empty = gtk_label_new(_("no masks on this photo yet"));
  dt_gui_add_class(d->empty, "dt_presets_empty");
  gtk_label_set_xalign(GTK_LABEL(d->empty), 0.0);
  gtk_widget_set_no_show_all(d->empty, TRUE);

  GtkWidget *add = gtk_button_new_with_label(_("+ new mask"));
  dt_gui_add_class(add, "dt_mask_layers_new");
  gtk_widget_set_tooltip_text(add, _("start a new masked adjustment"));
  g_signal_connect(add, "clicked", G_CALLBACK(_new_clicked), self);

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), d->list, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), d->empty, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), add, FALSE, FALSE, 0);

  if(!dt_conf_key_exists("plugins/darkroom/mask_layers/expanded"))
    dt_conf_set_bool("plugins/darkroom/mask_layers/expanded", TRUE);

  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_HISTORY_CHANGE, _queue_refresh);
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_IMAGE_CHANGED, _queue_refresh);
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_INITIALIZE, _queue_refresh);
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_MODULE_REMOVE, _queue_refresh);
  d->poll = g_timeout_add(400, _poll, self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = self->data;
  if(d && d->refresh) g_source_remove(d->refresh);
  if(d && d->poll) g_source_remove(d->poll);
  if(d) g_free(d->signature);
  g_free(self->data);
  self->data = NULL;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode c; remove-trailing-spaces modified;
// clang-format on
