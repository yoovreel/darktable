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

// mask adjustment: the quick sliders of the mask selected in the masks list,
// in the right column under the module groups. Like the quick access panel
// it shows controls that belong elsewhere: the "adjust" section of the
// selected mask's own panel is moved here while the mask is selected, and put
// back when it is not. Moving a slider adds the module instance that follows
// the mask, named after it, so the adjustments stay modules of their own.

#include "common/darktable.h"
#include "control/control.h"
#include "control/signal.h"
#include "develop/blend.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"

DT_MODULE(1)

typedef struct dt_lib_mask_adjust_t
{
  GtkWidget *holder;  // where the sliders are shown
  GtkWidget *empty;   // shown while no mask is selected
  guint poll;
  dt_iop_module_t *shown;  // the module whose sliders are here
  GtkWidget *box;          // those sliders
  GtkWidget *old_parent;   // where they go back to
  gint old_pos;
} dt_lib_mask_adjust_t;

const char *name(dt_lib_module_t *self)
{
  return _("mask adjustment");
}

const char *description(dt_lib_module_t *self)
{
  return _("quick sliders for the selected mask.\n"
           "moving one adds the module instance for it, named after the mask");
}

dt_view_type_flags_t views(dt_lib_module_t *self)
{
  return DT_VIEW_DARKROOM;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_RIGHT_TOP;
}

int position(const dt_lib_module_t *self)
{
  // just under the module groups (999)
  return 998;
}

static void _give_back(dt_lib_mask_adjust_t *d)
{
  if(!d->box) return;
  GtkWidget *box = d->box;
  d->box = NULL;
  d->shown = NULL;
  g_object_ref(box);
  if(gtk_widget_get_parent(box) == d->holder)
    gtk_container_remove(GTK_CONTAINER(d->holder), box);
  if(d->old_parent && GTK_IS_BOX(d->old_parent) && gtk_widget_get_parent(d->old_parent))
  {
    gtk_box_pack_start(GTK_BOX(d->old_parent), box, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(d->old_parent), box, d->old_pos);
  }
  g_object_unref(box);
  d->old_parent = NULL;
}

static dt_iop_module_t *_wanted(void)
{
  dt_iop_module_t *m = dt_dev_gui_module();
  dt_develop_t *dev = darktable.develop;
  if(!m || !dev || !g_list_find(dev->iop, m) || !m->blend_params) return NULL;
  if(!(m->blend_params->mask_mode & (DEVELOP_MASK_MASK | DEVELOP_MASK_FLEXI))) return NULL;
  return dt_iop_gui_blend_mask_adjust_box(m) ? m : NULL;
}

static void _sync(dt_lib_module_t *self)
{
  dt_lib_mask_adjust_t *d = self->data;
  dt_develop_t *dev = darktable.develop;
  // a module that is gone took its widgets with it
  if(d->shown && (!dev || !g_list_find(dev->iop, d->shown)))
  {
    if(d->box && gtk_widget_get_parent(d->box) == d->holder)
      gtk_container_remove(GTK_CONTAINER(d->holder), d->box);
    d->box = NULL;
    d->shown = NULL;
    d->old_parent = NULL;
  }

  dt_iop_module_t *want = _wanted();
  if(want == d->shown)
  {
    gtk_widget_set_visible(d->empty, want == NULL);
    return;
  }

  _give_back(d);
  if(want)
  {
    GtkWidget *box = dt_iop_gui_blend_mask_adjust_box(want);
    GtkWidget *parent = gtk_widget_get_parent(box);
    if(parent && GTK_IS_BOX(parent))
    {
      gint pos = 0;
      gtk_container_child_get(GTK_CONTAINER(parent), box, "position", &pos, NULL);
      d->old_parent = parent;
      d->old_pos = pos;
      g_object_ref(box);
      gtk_container_remove(GTK_CONTAINER(parent), box);
      gtk_box_pack_start(GTK_BOX(d->holder), box, FALSE, FALSE, 0);
      g_object_unref(box);
      d->box = box;
      d->shown = want;
      gtk_widget_show_all(box);
      dt_iop_gui_blend_mask_adjust_refresh(want);
    }
  }
  gtk_widget_set_visible(d->empty, d->shown == NULL);
}

static gboolean _poll(gpointer user_data)
{
  dt_lib_module_t *self = user_data;
  if(!self->widget || !gtk_widget_get_mapped(self->widget)) return G_SOURCE_CONTINUE;
  _sync(self);
  return G_SOURCE_CONTINUE;
}

static void _history_changed(gpointer instance, dt_lib_module_t *self)
{
  dt_lib_mask_adjust_t *d = self->data;
  // the sliders follow the instances, which may have been added or removed
  if(d && d->shown) dt_iop_gui_blend_mask_adjust_refresh(d->shown);
}

void view_leave(dt_lib_module_t *self, dt_view_t *old_view, dt_view_t *new_view)
{
  dt_lib_mask_adjust_t *d = self->data;
  if(d) _give_back(d);
}

void gui_init(dt_lib_module_t *self)
{
  dt_lib_mask_adjust_t *d = g_malloc0(sizeof(dt_lib_mask_adjust_t));
  self->data = d;

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  d->holder = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  d->empty = gtk_label_new(_("select a mask in the masks list to adjust it here"));
  gtk_label_set_xalign(GTK_LABEL(d->empty), 0.0);
  gtk_label_set_line_wrap(GTK_LABEL(d->empty), TRUE);
  dt_gui_add_class(d->empty, "dt_presets_empty");
  gtk_box_pack_start(GTK_BOX(self->widget), d->holder, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), d->empty, FALSE, FALSE, 0);
  gtk_widget_show_all(self->widget);

  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_HISTORY_CHANGE, _history_changed);
  d->poll = g_timeout_add(300, _poll, self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_lib_mask_adjust_t *d = self->data;
  if(d && d->poll) g_source_remove(d->poll);
  if(d) _give_back(d);
  g_free(self->data);
  self->data = NULL;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode c; remove-trailing-spaces modified;
// clang-format on
